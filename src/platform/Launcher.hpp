#pragma once

#include "platform/CommandLine.hpp"
#include "platform/WorkQueue.hpp"

// Starts commands on its own thread with ShellExecuteExW, so a UAC prompt (Task Manager needs one) never blocks the UI.
class Launcher {
  public:
    Launcher();

    // Queues the command. A full queue or a failed start is logged, never thrown: a click must not end the bar.
    void Launch(const Command& command);

  private:
    WorkQueue<Command> _queue;
};
