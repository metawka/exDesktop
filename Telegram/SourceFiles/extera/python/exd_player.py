import sys
import android_views
import exd_java
from exd_java import JavaObject

_state = {"alert": None}


# Menu

class ActionBarMenuSubItem(android_views.TextView):
    _java_name = "org.telegram.ui.ActionBar.ActionBarMenuSubItem"

    def __init__(self, context=None, *args):
        super().__init__(context)
        self.item_id = 0
        self.icon = 0
        self.shown = True
        self.checked = False

    def setTextAndIcon(self, text, icon=0, *args):
        self.setText(text)
        self.icon = icon

    def setIcon(self, icon):
        self.icon = icon

    def setChecked(self, value):
        self.checked = bool(value)

    def setVisibility(self, value):
        super().setVisibility(value)
        self.shown = int(value) == 0

    def getTextView(self):
        return self

    def setColors(self, *args):
        pass

    def setSelectorColor(self, *args):
        pass

    def setItemHeight(self, *args):
        pass

    def openSwipeBack(self):
        pass


class ActionBarMenuItem(android_views.View):
    _java_name = "org.telegram.ui.ActionBar.ActionBarMenuItem"

    def __init__(self, context=None, *args):
        super().__init__(context)
        self._items = []
        self._delegate = None
        self._listener = None

    def _find(self, item_id):
        for item in self._items:
            if item.item_id == int(item_id):
                return item
        return None

    def addSubItem(self, item_id, icon=0, text=None, *args):
        if text is None and not isinstance(icon, int):
            icon, text = 0, icon
        item = self._find(item_id) or ActionBarMenuSubItem()
        item.item_id = int(item_id)
        item.setTextAndIcon("" if text is None else str(text), icon)
        if item not in self._items:
            self._items.append(item)
        return item

    lazilyAddSubItem = addSubItem
    addSwipeBackItem = addSubItem

    def addItem(self, item_id=0, icon=0, *args):
        return self.addSubItem(item_id, icon, *args[:1])

    def addColoredGap(self, *args):
        return android_views.View()

    def addDivider(self, *args):
        return android_views.View()

    def getSubItem(self, item_id):
        return self._find(item_id)

    def setSubItemShown(self, item_id, shown):
        item = self._find(item_id)
        if item is not None:
            item.shown = bool(shown)

    def setSubItemVisibility(self, item_id, shown):
        self.setSubItemShown(item_id, shown)

    def showSubItem(self, item_id, *args):
        self.setSubItemShown(item_id, True)

    def hideSubItem(self, item_id):
        self.setSubItemShown(item_id, False)

    def isSubItemVisible(self, item_id):
        item = self._find(item_id)
        return bool(item and item.shown)

    def removeAllSubItems(self):
        self._items = []

    def hasSubMenu(self):
        return bool(self._items)

    def toggleSubMenu(self, *args):
        return None

    def closeSubMenu(self):
        return None

    def isSubMenuShowing(self):
        return False

    def setDelegate(self, delegate):
        self._delegate = delegate

    def setOnMenuDismiss(self, *args):
        pass

    def setShowSubmenuByMove(self, *args):
        pass

    def setSubMenuOpenSide(self, *args):
        pass

    def setIcon(self, *args):
        pass

    def setIconColor(self, *args):
        pass

    def getPopupLayout(self):
        return self

    def _visible(self):
        return [item for item in self._items if item.shown]

    def _clicked(self, item_id):
        item = self._find(item_id)
        if item is not None and item._click is not None:
            android_views.call_listener(item._click, "onClick", item)
        delegate = self._delegate
        if delegate is not None:
            android_views.call_listener(delegate, "onItemClick", int(item_id))


# Player

class AudioPlayerAlert(JavaObject):
    _java_name = "org.telegram.ui.Components.AudioPlayerAlert"

    def __init__(self, context=None, resources_provider=None, *args):
        self.parentActivity = context
        self.optionsButton = ActionBarMenuItem(context)
        self.menuItem = self.optionsButton
        self.playbackSpeedButton = ActionBarMenuItem(context)
        self.titleTextView = android_views.TextView(context)
        self.authorTextView = android_views.TextView(context)
        self._shown = False

    def onSubItemClick(self, item_id, *args):
        return None

    def updateTitle(self, animated=False, *args):
        import client_utils
        message = client_utils.get_media_controller().getPlayingMessageObject()
        if message is not None:
            try:
                self.titleTextView.setText(message.getMusicTitle())
                self.authorTextView.setText(message.getMusicAuthor())
            except Exception:
                pass

    def updateProgress(self, *args):
        return None

    def show(self):
        self._shown = True

    def dismiss(self):
        self._shown = False
        _state["alert"] = None if _state["alert"] is self else _state["alert"]

    def isShowing(self):
        return self._shown

    def getContainer(self):
        return self.optionsButton

    def didReceivedNotification(self, *args):
        return None


class TranscribeButton(JavaObject):
    _java_name = "org.telegram.ui.Components.TranscribeButton"

    def __init__(self, cell=None, message=None, *args):
        self.parent = cell
        self.messageObject = message if message is not None else (
            cell.getMessageObject() if callable(getattr(cell, "getMessageObject", None)) else None)

    def getMessageObject(self):
        return self.messageObject

    def onTap(self):
        return None

    def isOpen(self):
        return False

    def isLoading(self):
        return False

    def setOpen(self, *args):
        pass

    def setLoading(self, *args):
        pass

    @staticmethod
    def isTranscribing(message=None):
        return False

    @staticmethod
    def canTranscribeTrial(message=None):
        return True


# Compose

def _compose_log(error):
    try:
        import extera_host
        extera_host.log("compose field failed: %r" % (error,))
    except Exception:
        pass


def _compose_get():
    try:
        _extera = sys.modules["_extera"]
        return str(_extera.compose_get() or "")
    except Exception as error:
        _compose_log(error)
        return ""


def _compose_set(text, append=False):
    try:
        _extera = sys.modules["_extera"]
        _extera.compose_set(str(text), 1 if append else 0)
        return True
    except Exception as error:
        _compose_log(error)
        return False


class _Editable(JavaObject):
    _java_name = "android.text.Editable"

    def __init__(self, text):
        self._text = str(text)

    def toString(self):
        return self._text

    def length(self):
        return len(self._text)

    def charAt(self, index):
        return self._text[int(index)]

    def subSequence(self, start, end):
        return self._text[int(start):int(end)]

    def append(self, text):
        _compose_set(str(text), True)
        self._text += str(text)
        return self

    def insert(self, where, text):
        value = self._text[:int(where)] + str(text) + self._text[int(where):]
        _compose_set(value)
        self._text = value
        return self

    def replace(self, start, end, text, *args):
        value = self._text[:int(start)] + str(text) + self._text[int(end):]
        _compose_set(value)
        self._text = value
        return self

    def clear(self):
        _compose_set("")
        self._text = ""

    def __str__(self):
        return self._text

    def __len__(self):
        return len(self._text)


class EditTextCaption(android_views.EditText):
    _java_name = "org.telegram.ui.Components.EditTextCaption"

    def getText(self):
        return _Editable(_compose_get())

    def setText(self, text, *args):
        _compose_set("" if text is None else str(text))

    def append(self, text):
        _compose_set(str(text), True)

    def length(self):
        return len(_compose_get())

    def getSelectionStart(self):
        return len(_compose_get())

    def getSelectionEnd(self):
        return len(_compose_get())

    def setSelection(self, *args):
        pass

    def requestFocus(self, *args):
        return True

    def getCaption(self):
        return None

    def setCaption(self, *args):
        pass


class ChatActivityEnterView(android_views.View):
    _java_name = "org.telegram.ui.Components.ChatActivityEnterView"

    def __init__(self, fragment=None, *args):
        super().__init__(None)
        self._fragment = fragment
        self._edit = EditTextCaption(None)

    def getEditField(self):
        return self._edit

    getEditText = getEditField

    def getFieldText(self):
        text = _compose_get()
        return text if text else None

    def setFieldText(self, text, *args):
        _compose_set("" if text is None else str(text))

    def hasText(self):
        return bool(_compose_get().strip())

    def getParentFragment(self):
        return self._fragment

    def getReplyingMessageObject(self):
        return None

    def isInScheduleMode(self):
        return False

    def openKeyboard(self):
        pass

    def closeKeyboard(self):
        pass

    def hidePopup(self, *args):
        pass

    def getSendButton(self):
        return android_views.View(None)

    def isAttachedToWindow(self):
        return True

    def onSendButtonPressed(self, *args):
        return None

    def checkSendButton(self, *args):
        return None

    def updateScheduleButton(self, *args):
        return None


# Dialogs

class DialogCell(android_views.View):
    _java_name = "org.telegram.ui.Cells.DialogCell"

    def __init__(self, context=None, *args):
        super().__init__(context)
        self.currentDialogId = 0
        self.currentAccount = 0
        self.user = None
        self.chat = None
        self.nameString = ""
        self.message = None

    def getDialogId(self):
        return self.currentDialogId

    def getCurrentUser(self):
        return self.user

    def getCurrentChat(self):
        return self.chat

    def getMessage(self):
        return self.message

    def setDialog(self, dialog_id, *args):
        self.currentDialogId = int(getattr(dialog_id, "id", dialog_id) or 0)

    def update(self, mask=0, *args):
        return None

    def buildLayout(self):
        return None

    def isDialogCell(self):
        return True


def current():
    return _state["alert"]


def opened():
    import _compat
    alert = AudioPlayerAlert(_compat._LaunchActivity.instance, None)
    alert._shown = True
    _state["alert"] = alert
    alert.updateTitle(False)
    return alert


def closed():
    alert = _state["alert"]
    if alert is not None:
        alert._shown = False
    _state["alert"] = None


def menu_items():
    alert = _state["alert"]
    if alert is None:
        return []
    return [{"id": item.item_id, "text": str(item._props.get("text") or "")}
            for item in alert.optionsButton._visible()]


def menu_click(item_id):
    alert = _state["alert"]
    if alert is None:
        return False
    alert.onSubItemClick(int(item_id))
    alert.optionsButton._clicked(int(item_id))
    return True


def install(shims):
    import tgnet_bridge

    def enter_view(self):
        view = self.__dict__.get("_exd_enter_view")
        if view is None:
            view = ChatActivityEnterView(self)
            self.__dict__["_exd_enter_view"] = view
        return view
    tgnet_bridge.ChatActivity.getChatActivityEnterView = enter_view
    entries = {"org.telegram.ui.ActionBar": {"ActionBarMenuItem": ActionBarMenuItem,
                                             "ActionBarMenuSubItem": ActionBarMenuSubItem},
               "org.telegram.ui.Components": {"AudioPlayerAlert": AudioPlayerAlert,
                                              "TranscribeButton": TranscribeButton,
                                              "ChatActivityEnterView": ChatActivityEnterView,
                                              "EditTextCaption": EditTextCaption},
               "org.telegram.ui.Cells": {"DialogCell": DialogCell}}
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
        for value in values.values():
            exd_java._CLASSES[value._java_name] = value
