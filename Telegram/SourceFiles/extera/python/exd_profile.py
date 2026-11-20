import android_views
from exd_java import JavaObject

_ROW_NAMES = (
    "rowCount", "infoHeaderRow", "phoneRow", "locationRow", "userInfoRow", "channelInfoRow", "usernameRow",
    "birthdayRow", "bioRow", "infoStartRow", "infoEndRow", "infoSectionRow", "infoAffiliateRow", "affiliateRow",
    "notificationsDividerRow", "notificationsRow", "notificationsSimpleRow", "bizHoursRow", "bizLocationRow",
    "sendMessageRow", "reportRow", "reportReactionRow", "reportDividerRow", "addToContactsRow",
    "addToGroupButtonRow", "addToGroupInfoRow", "premiumRow", "starsRow", "businessRow", "premiumGiftingRow",
    "premiumSectionsRow", "botAppRow", "settingsTimerRow", "settingsKeyRow", "secretSettingsSectionRow",
    "membersHeaderRow", "membersStartRow", "membersEndRow", "addMemberRow", "subscribersRow",
    "subscribersRequestsRow", "administratorsRow", "settingsRow", "blockedUsersRow", "membersSectionRow",
    "sharedMediaRow", "unblockRow", "joinRow", "lastSectionRow", "channelRow", "channelDividerRow",
    "setAvatarRow", "setAvatarSectionRow", "numberSectionRow", "numberRow", "setUsernameRow", "emptyRow",
    "bottomPaddingRow", "versionRow", "botPermissionsHeader", "botPermissionsDivider", "balanceDividerRow",
    "channelBalanceRow", "channelBalanceSectionRow", "botStarsBalanceRow", "botTonBalanceRow",
)

_KINDS = (("phoneRow", "phone"), ("usernameRow", "username"), ("bioRow", "bio"), ("channelInfoRow", "bio"))
_LABELS = {"phone": "Mobile", "username": "Username", "bio": "Bio"}
_activities = {}


def _int(value, default=-1):
    try:
        return int(value)
    except Exception:
        return default


# Cells

class TextDetailCell(android_views.FrameLayout):
    _java_name = "org.telegram.ui.Cells.TextDetailCell"

    def __init__(self, context=None, *args):
        android_views.FrameLayout.__init__(self, context)
        self.textView = android_views.TextView(context)
        self.valueTextView = android_views.TextView(context)
        self.imageView = android_views.View(context)
        self.needDivider = False
        self.addView(self.textView)
        self.addView(self.valueTextView)

    def setTextAndValue(self, text, value, divider=False, *args):
        self.textView.setText(text)
        self.valueTextView.setText(value)
        self.needDivider = bool(divider)

    def setTextWithEmojiAndValue(self, text, value, divider=False, *args):
        self.setTextAndValue(text, value, divider)

    def setValue(self, value):
        self.valueTextView.setText(value)

    def getTextView(self):
        return self.textView

    def getValueTextView(self):
        return self.valueTextView

    def updateColors(self, *args):
        pass

    def setImage(self, *args):
        pass

    def setImageClickListener(self, *args):
        pass

    def setContentDescriptionValueFirst(self, *args):
        pass

    def _texts(self):
        return str(self.textView.getText() or ""), str(self.valueTextView.getText() or "")


class ViewHolder(JavaObject):
    _java_name = "androidx.recyclerview.widget.RecyclerView$ViewHolder"

    def __init__(self, view=None, position=-1, view_type=0):
        self.itemView = view
        self._position = int(position)
        self._type = int(view_type)

    def getAdapterPosition(self):
        return self._position

    getBindingAdapterPosition = getAdapterPosition
    getAbsoluteAdapterPosition = getAdapterPosition
    getLayoutPosition = getAdapterPosition

    def getItemViewType(self):
        return self._type


class ListAdapter(JavaObject):
    _java_name = "org.telegram.ui.ProfileActivity$ListAdapter"

    def __init__(self, activity):
        setattr(self, "this$0", activity)
        self._activity = activity

    def getItemCount(self):
        return max(_int(getattr(self._activity, "rowCount", 0), 0), 0)

    def getItemViewType(self, position):
        return 2

    def notifyDataSetChanged(self, *args):
        pass

    notifyItemChanged = notifyDataSetChanged
    notifyItemInserted = notifyDataSetChanged
    notifyItemRemoved = notifyDataSetChanged


# Rows

def activity(dialog_id):
    import tgnet_bridge
    dialog_id = int(dialog_id or 0)
    current = _activities.get(dialog_id)
    if current is None:
        current = tgnet_bridge.ProfileActivity(dialog_id)
        _activities.clear()
        _activities[dialog_id] = current
    return current


def _hooked(kinds):
    import extera_host
    for record in list(extera_host._records.values()):
        if record.instance is None:
            continue
        for kind in kinds:
            if record.instance.__dict__.get("_exd_virtual_" + kind):
                return True
    return False


def rows(dialog_id, info):
    import extera_host
    if not dialog_id or not _hooked(("profile_rows", "profile_bind")):
        return []
    act = activity(dialog_id)
    for name in _ROW_NAMES:
        setattr(act, name, -1)
    act.userId = dialog_id if dialog_id > 0 else 0
    act.chatId = -dialog_id if dialog_id < 0 else 0
    act.dialogId = dialog_id
    position = 0
    act.infoHeaderRow = position
    position += 1
    present = []
    bio_row = "bioRow" if dialog_id > 0 else "channelInfoRow"
    for name, key in (("phoneRow", "phone"), ("usernameRow", "username"), (bio_row, "bio")):
        if str(info.get(key) or ""):
            setattr(act, name, position)
            present.append(position)
            position += 1
    if present:
        act.infoStartRow = present[0]
        act.infoEndRow = present[-1]
    act.infoSectionRow = position
    position += 1
    act.rowCount = position
    extera_host._run_virtual("profile_rows", act, "ProfileActivity.updateRowsIds", [])
    kinds = {}
    for name, kind in _KINDS:
        value = _int(getattr(act, name, -1))
        if value >= 0 and str(info.get(kind) or ""):
            kinds[value] = kind
    skip = {_int(getattr(act, name, -1)) for name in ("infoHeaderRow", "infoSectionRow", "emptyRow",
                                                            "bottomPaddingRow", "lastSectionRow")}
    adapter = ListAdapter(act)
    result = []
    count = min(max(_int(getattr(act, "rowCount", 0), 0), 0), 64)
    for index in range(count):
        if index in skip:
            continue
        kind = kinds.get(index)
        cell = TextDetailCell(None)
        if kind:
            cell.setTextAndValue(str(info.get(kind) or ""), _LABELS[kind], False)
        before = cell._texts()
        holder = ViewHolder(cell, index, 2)
        extera_host._run_virtual("profile_bind", adapter, "ListAdapter.onBindViewHolder", [holder, index])
        text, value = cell._texts()
        if kind:
            if (text, value) != before:
                result.append({"kind": kind, "text": text, "value": value if value != before[1] else ""})
        elif text:
            result.append({"kind": "add", "text": text, "value": value})
    return result


def install(shims):
    import exd_java
    import tgnet_bridge
    for name in _ROW_NAMES:
        setattr(tgnet_bridge.ProfileActivity, name, -1)
    tgnet_bridge.ProfileActivity.rowCount = 0
    tgnet_bridge.ProfileActivity.userId = 0
    tgnet_bridge.ProfileActivity.chatId = 0
    entries = {"org.telegram.ui.Cells": {"TextDetailCell": TextDetailCell},
               "org.telegram.ui": {}}
    for module, values in entries.items():
        shims.setdefault(module, {}).update(values)
    for value in (TextDetailCell, ListAdapter):
        exd_java._CLASSES[value._java_name] = value
