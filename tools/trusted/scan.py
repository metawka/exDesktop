import hashlib
import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BADGES = ROOT / 'badges.json'
OUT = ROOT / 'trusted_hashes.json'
EXTENSIONS = ('.plugin', '.py')
MAX_SIZE = 20 * 1024 * 1024
MISS_LIMIT = 300
TOKEN = os.environ['TG_BOT_TOKEN']
DUMP = int(os.environ.get('TG_DUMP_CHAT') or 1388181090)
API = f'https://api.telegram.org/bot{TOKEN}/'
FILES = f'https://api.telegram.org/file/bot{TOKEN}/'
ADDED = []


# Api

class ApiError(Exception):
    pass


def call(method, **params):
    data = urllib.parse.urlencode({
        key: json.dumps(value) if isinstance(value, (list, dict, bool)) else value
        for key, value in params.items()
    }).encode()
    failures = 0
    while True:
        try:
            with urllib.request.urlopen(API + method, data, timeout=60) as response:
                return json.load(response)['result']
        except urllib.error.HTTPError as error:
            result = json.load(error)
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            failures += 1
            if failures > 5:
                raise
            time.sleep(10)
            continue
        retry = (result.get('parameters') or {}).get('retry_after')
        if retry:
            time.sleep(retry + 1)
            continue
        raise ApiError(result.get('description', ''))


def download(file_id):
    path = call('getFile', file_id=file_id)['file_path']
    with urllib.request.urlopen(FILES + path, timeout=120) as response:
        return response.read()


# State

def bare(chat):
    return -1000000000000 - chat


def load_state():
    try:
        state = json.loads(OUT.read_text(encoding='utf-8'))
    except Exception:
        state = {}
    state.setdefault('offset', 0)
    state.setdefault('scanned', {})
    state.setdefault('latest', {})
    state.setdefault('hashes', [])
    return state


def trusted_chats():
    sources = json.loads(BADGES.read_text(encoding='utf-8')).get('trusted', [])
    return {-1000000000000 - int(source['id']) for source in sources}


def add(state, known, message, chat, message_id):
    document = (message or {}).get('document') or {}
    name = document.get('file_name') or ''
    if (not name.lower().endswith(EXTENSIONS)
            or document.get('file_size', 0) > MAX_SIZE):
        return
    digest = hashlib.sha256(download(document['file_id'])).hexdigest()
    if digest in known:
        return
    known.add(digest)
    state['hashes'].append({
        'sha256': digest,
        'channel': bare(chat),
        'message': message_id,
        'name': name,
    })
    ADDED.append((chat, message_id, name))
    print(f'added {name} from {bare(chat)}/{message_id}')


# Updates

def origin(update):
    post = update.get('channel_post') or update.get('edited_channel_post')
    if post:
        return post, post['chat']['id'], post['message_id']
    message = update.get('message')
    forward = (message or {}).get('forward_origin') or {}
    if forward.get('type') == 'channel':
        return message, forward['chat']['id'], forward['message_id']
    return None, 0, 0


def read_updates(state, known, trusted):
    while True:
        updates = call(
            'getUpdates',
            offset=state['offset'],
            timeout=0,
            allowed_updates=['channel_post', 'edited_channel_post', 'message'])
        if not updates:
            return
        for update in updates:
            state['offset'] = update['update_id'] + 1
            message, chat, message_id = origin(update)
            if chat not in trusted:
                continue
            key = str(bare(chat))
            if update.get('channel_post'):
                state['latest'][key] = max(
                    state['latest'].get(key, 0), message_id)
            add(state, known, message, chat, message_id)


# History

def backfill(state, known, chat):
    key = str(bare(chat))
    latest = state['latest'].get(key, 0)
    message_id = 0
    misses = 0
    skipped = 0
    while message_id < latest or misses < MISS_LIMIT:
        message_id += 1
        try:
            message = call(
                'forwardMessage',
                chat_id=DUMP,
                from_chat_id=chat,
                message_id=message_id,
                disable_notification=True)
        except ApiError as error:
            text = str(error).lower()
            if 'message' in text and 'not found' in text:
                misses += 1
                continue
            if "can't be forwarded" in text:
                misses = 0
                skipped += 1
                continue
            print(f'history of {key} skipped: {error}')
            return
        misses = 0
        try:
            call('deleteMessage', chat_id=DUMP, message_id=message['message_id'])
        except ApiError:
            pass
        add(state, known, message, chat, message_id)
    state['scanned'][key] = message_id - misses
    print(f'history of {key} scanned up to {message_id - misses}, '
          f'not forwardable {skipped}')


# Report

def report():
    if not ADDED:
        return
    titles = {}
    lines = [f'Добавлено в доверенные плагины: {len(ADDED)}']
    for chat, message_id, name in ADDED:
        if chat not in titles:
            try:
                info = call('getChat', chat_id=chat)
                titles[chat] = (info.get('title') or str(bare(chat)), info.get('username'))
            except ApiError:
                titles[chat] = (str(bare(chat)), None)
        title, username = titles[chat]
        link = (f'https://t.me/{username}/{message_id}' if username
                else f'https://t.me/c/{bare(chat)}/{message_id}')
        lines.append(f'{name} · {title} · {link}')
    text = ''
    for line in lines:
        if len(text) + len(line) + 1 > 4000:
            call('sendMessage', chat_id=DUMP, text=text,
                 disable_web_page_preview=True)
            text = ''
        text += line + '\n'
    if text:
        call('sendMessage', chat_id=DUMP, text=text,
             disable_web_page_preview=True)


# Main

def main():
    trusted = trusted_chats()
    state = load_state()
    known = {entry['sha256'] for entry in state['hashes']}
    try:
        read_updates(state, known, trusted)
        for chat in sorted(trusted):
            if str(bare(chat)) not in state['scanned']:
                backfill(state, known, chat)
    finally:
        OUT.write_text(
            json.dumps(state, ensure_ascii=False, indent=2) + '\n',
            encoding='utf-8', newline='\n')
        try:
            report()
        except Exception as error:
            print(f'report failed: {error}')


if __name__ == '__main__':
    main()
