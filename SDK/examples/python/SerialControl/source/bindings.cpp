#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <serial/serial_service.hpp>

namespace py = pybind11;
using namespace doly::serial;

PYBIND11_MODULE(SerialControl, m) {
    py::class_<SerialConfig>(m, "SerialConfig")
        .def(py::init<>())
        .def_readwrite("device", &SerialConfig::device)
        .def_readwrite("baud", &SerialConfig::baud)
        .def_readwrite("use_simulator", &SerialConfig::use_simulator)
        .def_readwrite("sim_file", &SerialConfig::sim_file);

    py::class_<SerialService>(m, "SerialService")
        .def(py::init<>())
        .def("init", &SerialService::init)
        .def("start", &SerialService::start)
        .def("stop", &SerialService::stop)
        .def("set_handler", &SerialService::set_handler);
}
