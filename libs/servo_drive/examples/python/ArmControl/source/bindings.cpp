#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "ArmControl.h"
#include "ArmEvent.h"
#include "ArmEventListener.h"

namespace py = pybind11;

static std::atomic<bool> g_callbacks_enabled{true};

static std::mutex g_cb_mutex;
static py::function g_on_complete;
static py::function g_on_error;
static py::function g_on_state_change;
static py::function g_on_movement;
static bool g_complete_registered = false;
static bool g_error_registered = false;
static bool g_state_registered = false;
static bool g_movement_registered = false;

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

class PyArmEventListener : public ArmEventListener {
public:
    using ArmEventListener::ArmEventListener;

    void onArmComplete(uint16_t id, ArmSide side) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ArmEventListener*>(this), "on_arm_complete");
        safe_call(override, id, side);
    }

    void onArmError(uint16_t id, ArmSide side, ArmErrorType error_type) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ArmEventListener*>(this), "on_arm_error");
        safe_call(override, id, side, error_type);
    }

    void onArmStateChange(ArmSide side, ArmState state) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ArmEventListener*>(this), "on_arm_state_change");
        safe_call(override, side, state);
    }

    void onArmMovement(ArmSide side, float degree_change) override {
        if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
            return;
        }

        py::gil_scoped_acquire gil;
        py::function override = py::get_override(static_cast<const ArmEventListener*>(this), "on_arm_movement");
        safe_call(override, side, degree_change);
    }
};

static void on_complete_trampoline(uint16_t id, ArmSide side) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_complete;
    }
    safe_call(cb, id, side);
}

static void on_error_trampoline(uint16_t id, ArmSide side, ArmErrorType error_type) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_error;
    }
    safe_call(cb, id, side, error_type);
}

static void on_state_change_trampoline(ArmSide side, ArmState state) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_state_change;
    }
    safe_call(cb, side, state);
}

static void on_movement_trampoline(ArmSide side, float degree_change) {
    if (!g_callbacks_enabled.load(std::memory_order_relaxed)) {
        return;
    }

    py::gil_scoped_acquire gil;
    py::function cb;
    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);
        cb = g_on_movement;
    }
    safe_call(cb, side, degree_change);
}

static void clear_listeners_impl() {
    g_callbacks_enabled.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (g_complete_registered) {
            ArmEvent::RemoveListenerOnComplete(on_complete_trampoline);
            g_complete_registered = false;
        }
        if (g_error_registered) {
            ArmEvent::RemoveListenerOnError(on_error_trampoline);
            g_error_registered = false;
        }
        if (g_state_registered) {
            ArmEvent::RemoveListenerOnStateChange(on_state_change_trampoline);
            g_state_registered = false;
        }
        if (g_movement_registered) {
            ArmEvent::RemoveListenerOnMovement(on_movement_trampoline);
            g_movement_registered = false;
        }

        g_on_complete = py::function();
        g_on_error = py::function();
        g_on_state_change = py::function();
        g_on_movement = py::function();
    }

    std::vector<py::object> listeners;
    {
        std::lock_guard<std::mutex> lock(g_listener_mutex);
        listeners.swap(g_py_listeners);
    }

    for (auto& obj : listeners) {
        try {
            auto* ptr = obj.cast<ArmEventListener*>();
            if (ptr) {
                ArmEvent::RemoveListener(ptr);
            }
        } catch (const py::cast_error&) {
        }
    }
}

PYBIND11_MODULE(doly_arm, m) {
    m.doc() = "Python bindings for DOLY ArmControl";

    py::enum_<ArmErrorType>(m, "ArmErrorType")
        .value("Abort", ArmErrorType::ABORT)
        .value("Motor", ArmErrorType::MOTOR)
        .export_values();

    py::enum_<ArmSide>(m, "ArmSide")
        .value("Both", ArmSide::BOTH)
        .value("Left", ArmSide::LEFT)
        .value("Right", ArmSide::RIGHT)
        .export_values();

    py::enum_<ArmState>(m, "ArmState")
        .value("Running", ArmState::RUNNING)
        .value("Completed", ArmState::COMPLETED)
        .value("Error", ArmState::ERROR)
        .export_values();

    py::class_<ArmData>(m, "ArmData")
        .def(py::init<>())
        .def_readwrite("side", &ArmData::side)
        .def_readwrite("angle", &ArmData::angle);

    py::class_<ArmEventListener, PyArmEventListener>(m, "ArmEventListener")
        .def(py::init<>())
        .def("on_arm_complete", &ArmEventListener::onArmComplete)
        .def("on_arm_error", &ArmEventListener::onArmError)
        .def("on_arm_state_change", &ArmEventListener::onArmStateChange)
        .def("on_arm_movement", &ArmEventListener::onArmMovement);

    m.def("add_listener", [](py::object listener_obj, bool priority) {
        if (listener_obj.is_none()) {
            return;
        }

        auto* ptr = listener_obj.cast<ArmEventListener*>();
        if (!ptr) {
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(g_listener_mutex);
            for (auto& obj : g_py_listeners) {
                try {
                    if (obj.cast<ArmEventListener*>() == ptr) {
                        return;
                    }
                } catch (const py::cast_error&) {
                }
            }
            g_py_listeners.push_back(listener_obj);
        }

        ArmEvent::AddListener(ptr, priority);
    }, py::arg("listener"), py::arg("priority") = false);

    m.def("remove_listener", [](py::object listener_obj) {
        if (listener_obj.is_none()) {
            return;
        }

        auto* ptr = listener_obj.cast<ArmEventListener*>();
        if (!ptr) {
            return;
        }

        ArmEvent::RemoveListener(ptr);

        std::lock_guard<std::mutex> lock(g_listener_mutex);
        g_py_listeners.erase(
            std::remove_if(
                g_py_listeners.begin(),
                g_py_listeners.end(),
                [&](py::object& obj) {
                    try {
                        return obj.cast<ArmEventListener*>() == ptr;
                    } catch (const py::cast_error&) {
                        return false;
                    }
                }),
            g_py_listeners.end());
    }, py::arg("listener"));

    m.def("clear_listeners", []() {
        clear_listeners_impl();
    });

        m.def("init", &ArmControl::init, py::call_guard<py::gil_scoped_release>());
    m.def("dispose", []() {
        clear_listeners_impl();
        return ArmControl::dispose();
        }, py::call_guard<py::gil_scoped_release>());
        m.def("is_active", &ArmControl::isActive, py::call_guard<py::gil_scoped_release>());
        m.def("abort", &ArmControl::Abort, py::arg("side"), py::call_guard<py::gil_scoped_release>());
    m.def("get_max_angle", &ArmControl::getMaxAngle);
    m.def("set_angle", &ArmControl::setAngle,
          py::arg("id"),
          py::arg("side"),
          py::arg("speed"),
          py::arg("angle"),
            py::arg("with_brake") = false,
            py::call_guard<py::gil_scoped_release>());
    m.def("get_state", &ArmControl::getState, py::arg("side"));
    m.def("get_current_angle", &ArmControl::getCurrentAngle, py::arg("side"));
    m.def("get_version", &ArmControl::getVersion);

    m.def("move_multi_duration", &ArmControl::moveMultiDuration,
            py::arg("targets"), py::arg("duration_ms"),
            py::call_guard<py::gil_scoped_release>());
    m.def("servo_swing_of", &ArmControl::servoSwingOf,
          py::arg("side"),
          py::arg("target_angle"),
          py::arg("approach_speed"),
          py::arg("swing_amplitude"),
          py::arg("swing_speed"),
            py::arg("count") = -1,
            py::call_guard<py::gil_scoped_release>());
    m.def("start_swing", &ArmControl::startSwing,
          py::arg("side"),
          py::arg("min_angle"),
          py::arg("max_angle"),
          py::arg("duration_one_way"),
            py::arg("count"),
            py::call_guard<py::gil_scoped_release>());
    m.def("lift_dumbbell", &ArmControl::liftDumbbell,
            py::arg("side"), py::arg("weight"), py::arg("reps"),
            py::call_guard<py::gil_scoped_release>());
    m.def("dumbbell_dance", &ArmControl::dumbbellDance,
            py::arg("weight"), py::arg("duration_sec"),
            py::call_guard<py::gil_scoped_release>());
    m.def("wave_flag", &ArmControl::waveFlag,
            py::arg("side"), py::arg("flag_weight"), py::arg("wave_count"),
            py::call_guard<py::gil_scoped_release>());
    m.def("beat_drum", &ArmControl::beatDrum,
            py::arg("side"), py::arg("stick_weight"), py::arg("beat_count"),
            py::call_guard<py::gil_scoped_release>());
    m.def("paddle_row", &ArmControl::paddleRow,
            py::arg("side"), py::arg("paddle_weight"), py::arg("stroke_count"),
            py::call_guard<py::gil_scoped_release>());
    m.def("dual_paddle_row", &ArmControl::dualPaddleRow,
            py::arg("paddle_weight"), py::arg("stroke_count"),
            py::call_guard<py::gil_scoped_release>());

    m.def("on_complete", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_complete_registered) {
                ArmEvent::RemoveListenerOnComplete(on_complete_trampoline);
                g_complete_registered = false;
            }
            g_on_complete = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_complete = cb_obj.cast<py::function>();
        if (!g_complete_registered) {
            ArmEvent::AddListenerOnComplete(on_complete_trampoline);
            g_complete_registered = true;
        }
    });

    m.def("on_error", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_error_registered) {
                ArmEvent::RemoveListenerOnError(on_error_trampoline);
                g_error_registered = false;
            }
            g_on_error = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_error = cb_obj.cast<py::function>();
        if (!g_error_registered) {
            ArmEvent::AddListenerOnError(on_error_trampoline);
            g_error_registered = true;
        }
    });

    m.def("on_state_change", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_state_registered) {
                ArmEvent::RemoveListenerOnStateChange(on_state_change_trampoline);
                g_state_registered = false;
            }
            g_on_state_change = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_state_change = cb_obj.cast<py::function>();
        if (!g_state_registered) {
            ArmEvent::AddListenerOnStateChange(on_state_change_trampoline);
            g_state_registered = true;
        }
    });

    m.def("on_movement", [](py::object cb_obj) {
        std::lock_guard<std::mutex> lock(g_cb_mutex);

        if (cb_obj.is_none()) {
            if (g_movement_registered) {
                ArmEvent::RemoveListenerOnMovement(on_movement_trampoline);
                g_movement_registered = false;
            }
            g_on_movement = py::function();
            return;
        }

        g_callbacks_enabled.store(true, std::memory_order_relaxed);
        g_on_movement = cb_obj.cast<py::function>();
        if (!g_movement_registered) {
            ArmEvent::AddListenerOnMovement(on_movement_trampoline);
            g_movement_registered = true;
        }
    });
}