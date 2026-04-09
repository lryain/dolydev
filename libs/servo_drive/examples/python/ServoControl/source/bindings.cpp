#include <pybind11/pybind11.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "ServoControl.h"
#include "ServoEvent.h"
#include "ServoEventListener.h"

namespace py = pybind11;

static std::atomic<bool> g_callbacks_enabled{true};

static std::mutex g_cb_mutex;
static py::function g_on_complete;
static py::function g_on_abort;
static py::function g_on_error;
static bool g_complete_registered = false;
static bool g_abort_registered = false;
static bool g_error_registered = false;

static std::mutex g_listener_mutex;
static std::vector<py::object> g_py_listeners;

template <typename... Args>
static void safe_call(const py::function& fn, Args&&... args) {
    if (!fn) {
        return;
    }

    try {
        fn(std::forward<Args>(args)...);
    } catch (const py::error_already_set&) {
        PyErr_Print();
    }
}

class PyServoEventListener : public ServoEventListener {
public:
    using ServoEventListener::ServoEventListener;

    void onServoAbort(uint16_t id, ServoId channel) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ServoEventListener*>(this), "on_servo_abort");
        safe_call(override, id, channel);
    }

    void onServoError(uint16_t id, ServoId channel) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ServoEventListener*>(this), "on_servo_error");
        safe_call(override, id, channel);
    }

    void onServoComplete(uint16_t id, ServoId channel) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ServoEventListener*>(this), "on_servo_complete");
        safe_call(override, id, channel);
    }
};

static void on_complete_trampoline(uint16_t id, ServoId channel) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_complete;
    }
    safe_call(cb, id, channel);
}

static void on_abort_trampoline(uint16_t id, ServoId channel) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_abort;
    }
    safe_call(cb, id, channel);
}

static void on_error_trampoline(uint16_t id, ServoId channel) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_error;
    }
    safe_call(cb, id, channel);
}

static void clear_listeners_impl() {
    g_callbacks_enabled.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (g_complete_registered) {
            ServoEvent::RemoveListenerOnComplete(on_complete_trampoline);
            g_complete_registered = false;
        }
        if (g_abort_registered) {
            ServoEvent::RemoveListenerOnAbort(on_abort_trampoline);
            g_abort_registered = false;
        }
        if (g_error_registered) {
            ServoEvent::RemoveListenerOnError(on_error_trampoline);
            g_error_registered = false;
        }

        g_on_complete = py::function();
        g_on_abort = py::function();
        g_on_error = py::function();
    }

    std::vector<py::object> listeners;
    {
        std::lock_guard<std::mutex> lock(g_listener_mutex);
        listeners.swap(g_py_listeners);
    }

    for (auto& obj : listeners) {
        try {
            auto* ptr = obj.cast<ServoEventListener*>();
            if (ptr) {
                ServoEvent::RemoveListener(ptr);
            }
        } catch (const py::cast_error&) {
        }
    }
}

PYBIND11_MODULE(doly_servo, m) {
    m.doc() = "Python bindings for DOLY ServoControl";

    py::enum_<ServoId>(m, "ServoId")
        .value("Servo0", ServoId::SERVO_0)
        .value("Servo1", ServoId::SERVO_1)
        .export_values();

    py::class_<ServoEventListener, PyServoEventListener>(m, "ServoEventListener")
        .def(py::init<>())
        .def("on_servo_complete", &ServoEventListener::onServoComplete)
        .def("on_servo_abort", &ServoEventListener::onServoAbort)
        .def("on_servo_error", &ServoEventListener::onServoError);

    m.def("add_listener", [](py::object listener_obj, bool priority) {
        if (listener_obj.is_none()) {
            return;
        }

        auto* ptr = listener_obj.cast<ServoEventListener*>();
        if (!ptr) {
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(g_listener_mutex);
            for (auto& obj : g_py_listeners) {
                try {
                    if (obj.cast<ServoEventListener*>() == ptr) {
                        return;
                    }
                } catch (const py::cast_error&) {
                }
            }
            g_py_listeners.push_back(listener_obj);
        }

        ServoEvent::AddListener(ptr, priority);
    }, py::arg("listener"), py::arg("priority") = false);

    m.def("remove_listener", [](py::object listener_obj) {
        if (listener_obj.is_none()) {
            return;
        }

        auto* ptr = listener_obj.cast<ServoEventListener*>();
        if (!ptr) {
            return;
        }

        ServoEvent::RemoveListener(ptr);

        std::lock_guard<std::mutex> lock(g_listener_mutex);
        g_py_listeners.erase(
            std::remove_if(
                g_py_listeners.begin(),
                g_py_listeners.end(),
                [&](py::object& obj) {
                    try {
                        return obj.cast<ServoEventListener*>() == ptr;
                    } catch (const py::cast_error&) {
                        return false;
                    }
                }),
            g_py_listeners.end());
    }, py::arg("listener"));

    m.def("clear_listeners", []() {
        clear_listeners_impl();
    });

        m.def("init", &ServoControl::init, py::call_guard<py::gil_scoped_release>());
    m.def("dispose", []() {
        clear_listeners_impl();
        return ServoControl::dispose();
        }, py::call_guard<py::gil_scoped_release>());
    m.def("set_servo", &ServoControl::setServo,
          py::arg("id"),
          py::arg("channel"),
          py::arg("angle"),
          py::arg("speed"),
            py::arg("invert") = false,
            py::call_guard<py::gil_scoped_release>());
        m.def("abort", &ServoControl::abort, py::arg("channel"), py::call_guard<py::gil_scoped_release>());
        m.def("release", &ServoControl::release, py::arg("channel"), py::call_guard<py::gil_scoped_release>());
    m.def("get_version", &ServoControl::getVersion);

    m.def("on_complete", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_complete_registered) {
                ServoEvent::RemoveListenerOnComplete(on_complete_trampoline);
                g_complete_registered = false;
            }
            g_on_complete = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_complete = cb_obj.cast<py::function>();
        if (!g_complete_registered) {
            ServoEvent::AddListenerOnComplete(on_complete_trampoline);
            g_complete_registered = true;
        }
    });

    m.def("on_abort", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_abort_registered) {
                ServoEvent::RemoveListenerOnAbort(on_abort_trampoline);
                g_abort_registered = false;
            }
            g_on_abort = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_abort = cb_obj.cast<py::function>();
        if (!g_abort_registered) {
            ServoEvent::AddListenerOnAbort(on_abort_trampoline);
            g_abort_registered = true;
        }
    });

    m.def("on_error", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_error_registered) {
                ServoEvent::RemoveListenerOnError(on_error_trampoline);
                g_error_registered = false;
            }
            g_on_error = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_error = cb_obj.cast<py::function>();
        if (!g_error_registered) {
            ServoEvent::AddListenerOnError(on_error_trampoline);
            g_error_registered = true;
        }
    });
}