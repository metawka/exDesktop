ELYX_PLUGIN_NAMESPACE = "elyx"


def get_plugin_id(fn=None):
    try:
        import extera_host
        return extera_host.current_plugin_id()
    except Exception:
        return None
