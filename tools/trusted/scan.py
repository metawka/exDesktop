import hashlib
import json
import os
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BADGES = ROOT / 'badges.json'
OUT = ROOT / 'trusted_hashes.json'
EXTENSIONS = ('.plugin', '.py')
MAX_SIZE = 20 * 1024 * 1024
TOKEN = os.environ['TG_BOT_TOKEN']
API = f'https://api.telegram.org/bot{TOKEN}/'
FILES = f'https://api.telegram.org/file/bot{TOKEN}/'


# Api

def call(method, **params):
    data = urllib.parse.urlencode({
        key: json.dumps(value) if isinstance(value, (list, dict)) else value
        for key, value in params.items()
    }).encode()
    with urllib.request.urlopen(API + method, data, timeout=60) as response:
        result = json.load(response)
    if not result.get('ok'):
        raise RuntimeError(f'{method}: {result}')
    return result['result']


def download(file_id):
    path = call('getFile', file_id=file_id)['file_path']
    with urllib.request.urlopen(FILES + path, timeout=120) as response:
        return response.read()


# State

def load_state():
    try:
        state = json.loads(OUT.read_text(encoding='utf-8'))
    except Exception:
        state = {}
    state.setdefault('offset', 0)
    state.setdefault('hashes', [])
    return state


def trusted_ids():
    sources = json.loads(BADGES.read_text(encoding='utf-8')).get('trusted', [])
    return {-1000000000000 - int(source['id']) for source in sources}


def origin(update):
    post = update.get('channel_post') or update.get('edited_channel_post')
    if post:
        return post, post['chat']['id'], post['message_id']
    message = update.get('message')
    forward = (message or {}).get('forward_origin') or {}
    if forward.get('type') == 'channel':
        return message, forward['chat']['id'], forward['message_id']
    return None, 0, 0


# Main

def main():
    trusted = trusted_ids()
    state = load_state()
    known = {entry['sha256'] for entry in state['hashes']}
    while True:
        updates = call(
            'getUpdates',
            offset=state['offset'],
            timeout=0,
            allowed_updates=['channel_post', 'edited_channel_post', 'message'])
        if not updates:
            break
        for update in updates:
            state['offset'] = update['update_id'] + 1
            message, chat, message_id = origin(update)
            document = (message or {}).get('document') or {}
            name = document.get('file_name') or ''
            if (chat not in trusted
                    or not name.lower().endswith(EXTENSIONS)
                    or document.get('file_size', 0) > MAX_SIZE):
                continue
            digest = hashlib.sha256(download(document['file_id'])).hexdigest()
            if digest in known:
                continue
            known.add(digest)
            state['hashes'].append({
                'sha256': digest,
                'channel': -1000000000000 - chat,
                'message': message_id,
                'name': name,
            })
            print(f'added {name} from {chat}/{message_id}')
    OUT.write_text(
        json.dumps(state, ensure_ascii=False, indent=2) + '\n',
        encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
