#include <Python.h>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static std::string g_lastLog;
static std::string g_lastToast;
static std::string g_lastCopied;

static PyObject *NativeLog(PyObject *self, PyObject *args) {
    const char *text = nullptr;
    if (PyArg_ParseTuple(args, "s", &text)) {
        g_lastLog = text;
        std::cout << "[NativeLog] " << text << std::endl;
    }
    Py_RETURN_NONE;
}

static PyObject *NativeToast(PyObject *self, PyObject *args) {
    const char *text = nullptr;
    if (PyArg_ParseTuple(args, "s", &text)) {
        g_lastToast = text;
        std::cout << "[NativeToast] " << text << std::endl;
    }
    Py_RETURN_NONE;
}

static PyObject *NativeCopy(PyObject *self, PyObject *args) {
    const char *text = nullptr;
    if (PyArg_ParseTuple(args, "s", &text)) {
        g_lastCopied = text;
        std::cout << "[NativeCopy] " << text << std::endl;
    }
    Py_RETURN_NONE;
}

static PyObject *NativeSendMessage(PyObject *self, PyObject *args) {
    long long peerId = 0;
    const char *text = nullptr;
    const char *entities = nullptr;
    long long replyTo = 0;
    if (PyArg_ParseTuple(args, "LssL", &peerId, &text, &entities, &replyTo)) {
        std::cout << "[NativeSendMessage] peer: " << peerId << ", text: " << text << std::endl;
    }
    Py_RETURN_NONE;
}

static PyObject *NativeSettingsChanged(PyObject *self, PyObject *args) {
    const char *pluginId = nullptr;
    if (PyArg_ParseTuple(args, "s", &pluginId)) {
        std::cout << "[NativeSettingsChanged] " << pluginId << std::endl;
    }
    Py_RETURN_NONE;
}

static PyObject *NativeCurrentPeer(PyObject *self, PyObject *args) {
    return PyLong_FromLongLong(123456789LL);
}

static PyMethodDef NativeMethods[] = {
    { "log", NativeLog, METH_VARARGS, nullptr },
    { "toast", NativeToast, METH_VARARGS, nullptr },
    { "copy", NativeCopy, METH_VARARGS, nullptr },
    { "send_message", NativeSendMessage, METH_VARARGS, nullptr },
    { "settings_changed", NativeSettingsChanged, METH_VARARGS, nullptr },
    { "current_peer", NativeCurrentPeer, METH_VARARGS, nullptr },
    { nullptr, nullptr, 0, nullptr },
};

static PyModuleDef NativeModule = {
    PyModuleDef_HEAD_INIT,
    "_extera",
    nullptr,
    -1,
    NativeMethods,
};

static PyObject *InitNativeModule() {
    return PyModule_Create(&NativeModule);
}

int main(int argc, char **argv) {
    fs::path repoDir = fs::current_path();
    fs::path pythonDir = repoDir / "Telegram" / "SourceFiles" / "extera" / "python";
    if (!fs::exists(pythonDir / "extera_host.py")) {
        std::cerr << "Cannot find extera_host.py in " << pythonDir << std::endl;
        return 1;
    }

    fs::path tempBase = fs::temp_directory_path() / "exdesktop_test";
    fs::remove_all(tempBase);
    fs::create_directories(tempBase / "plugins");
    fs::create_directories(tempBase / "settings");

    // Create a dummy sample plugin (.plugin extension)
    fs::path samplePluginPath = tempBase / "plugins" / "sample.plugin";
    {
        std::ofstream out(samplePluginPath);
        out << R"PY(
__id__ = "sample_test"
__name__ = "Sample Test Plugin"
__description__ = "A test plugin for Linux native runner"
__author__ = "exDesktop Porter"
__version__ = "1.0.0"

import _extera
from base_plugin import BasePlugin, HookResult, HookStrategy

class TestPlugin(BasePlugin):
    def on_plugin_load(self):
        _extera.log("TestPlugin loaded on Linux!")
        self.add_on_send_message_hook(priority=10)

    def on_send_message_hook(self, account, params):
        _extera.log(f"on_send_message_hook triggered: message={params.message}")
        if params.message.startswith("/shout "):
            params.message = params.message[7:].upper() + "!!!"
            return HookResult(HookStrategy.MODIFY, params=params)
        return HookResult(HookStrategy.DEFAULT)
)PY";
    }

    std::cout << "[TEST] 1. Registering native inittab..." << std::endl;
    if (PyImport_AppendInittab("_extera", InitNativeModule) == -1) {
        std::cerr << "Failed to register _extera inittab" << std::endl;
        return 1;
    }

    std::cout << "[TEST] 2. Initializing Python from Config (Linux PyConfig_InitPythonConfig)..." << std::endl;
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    config.write_bytecode = 0;
    config.install_signal_handlers = 0;

    PyStatus status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        std::cerr << "Py_InitializeFromConfig failed: " << (status.err_msg ? status.err_msg : "unknown") << std::endl;
        PyConfig_Clear(&config);
        return 1;
    }
    PyConfig_Clear(&config);

    std::cout << "[TEST] 3. Injecting module search paths into sys.path..." << std::endl;
    PyObject *sysPath = PySys_GetObject("path");
    assert(sysPath && PyList_Check(sysPath));

    PyObject *pyPath = PyUnicode_FromString(pythonDir.c_str());
    PyList_Insert(sysPath, 0, pyPath);
    Py_DECREF(pyPath);

    std::cout << "[TEST] 4. Importing extera_host module..." << std::endl;
    PyObject *hostModule = PyImport_ImportModule("extera_host");
    if (!hostModule) {
        PyErr_Print();
        std::cerr << "Failed to import extera_host!" << std::endl;
        return 1;
    }

    PyObject *dispatch = PyObject_GetAttrString(hostModule, "dispatch");
    assert(dispatch != nullptr);

    auto callDispatch = [&](const std::string &method, const std::string &payload) -> std::string {
        PyObject *res = PyObject_CallFunction(dispatch, "ss", method.c_str(), payload.c_str());
        if (!res) {
            PyErr_Print();
            return "";
        }
        std::string out = PyUnicode_AsUTF8(res);
        Py_DECREF(res);
        return out;
    };

    std::cout << "[TEST] 5. Calling dispatch('init')..." << std::endl;
    std::string initPayload = "{\"plugins\": \"" + (tempBase / "plugins").string() + "\", \"settings\": \"" + (tempBase / "settings").string() + "\", \"safe_mode\": false}";
    std::string initRes = callDispatch("init", initPayload);
    std::cout << "init response: " << initRes << std::endl;
    assert(initRes.find("\"ok\": true") != std::string::npos);

    std::cout << "[TEST] 6. Calling dispatch('list')..." << std::endl;
    std::string listRes = callDispatch("list", "{}");
    std::cout << "list response: " << listRes << std::endl;
    assert(listRes.find("sample_test") != std::string::npos);
    assert(listRes.find("Sample Test Plugin") != std::string::npos);

    std::cout << "[TEST] 7. Testing send_hook with message modification..." << std::endl;
    std::string sendHookPayload = "{\"history\": 1, \"text\": \"/shout hello linux\", \"reply_to\": 0}";
    std::string hookRes = callDispatch("send_hook", sendHookPayload);
    std::cout << "send_hook response: " << hookRes << std::endl;
    assert(hookRes.find("HELLO LINUX!!!") != std::string::npos);

    std::cout << "[TEST] 8. Testing shutdown..." << std::endl;
    std::string shutRes = callDispatch("shutdown", "{}");
    std::cout << "shutdown response: " << shutRes << std::endl;
    assert(shutRes.find("\"ok\": true") != std::string::npos);

    Py_DECREF(dispatch);
    Py_DECREF(hostModule);
    Py_Finalize();

    fs::remove_all(tempBase);
    std::cout << "\n==============================================" << std::endl;
    std::cout << "ALL TESTS PASSED: extera_plugins engine works flawlessly on Linux!" << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
