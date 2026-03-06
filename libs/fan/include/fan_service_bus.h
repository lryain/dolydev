#ifndef FAN_SERVICE_BUS_H
#define FAN_SERVICE_BUS_H

// Entry point function for fan service logic.  The executable provides a
// small main() that forwards to this so the library can be linked independently
// (e.g. for testing or reuse).

int runFanService(int argc, char** argv);

#endif // FAN_SERVICE_BUS_H
