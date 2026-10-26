#include <gtest/gtest.h>

#include <sched.h>

#include <iostream>

#include "launch_core/launch_interpreter.hpp"

using namespace basin::launch_core;

TEST(LaunchInterpreterTest, test_load_cycle_interpreter) {
    // Load the interpreter
    LaunchInterpreter interp;
    std::vector<ProcessDescription> processes;

    // Load an inital test launch description
    interp.loadDescription("launch_api", "test.prod.py");
    processes = interp.getProcessDescription();

    // Verify we have at least one process
    ASSERT_GT(processes.size(), 0);

    // Try and force a reload here
    interp.loadDescription("launch_api", "test.prod.py");
    processes = interp.getProcessDescription();

    // Verify we have at least one process
    ASSERT_GT(processes.size(), 0);

    for (ProcessDescription descrip : processes) {
        // Verify each launch item
        ASSERT_GT(descrip.exe_path.string().size(), 0);
        ASSERT_GE(descrip.args.size(), 0);
        for (const std::string& arg : descrip.args) {
            ASSERT_GT(arg.size(), 0);
        }

        // Only the talker loaded from test2.prod.py sets scheduler options
        if (descrip.process_name == "/hurr/talker") {
            ASSERT_TRUE(descrip.scheduling.has_value());
            EXPECT_EQ(descrip.scheduling->policy, SCHED_BATCH);
            EXPECT_EQ(descrip.scheduling->priority, 0);
            ASSERT_TRUE(descrip.scheduling->nice.has_value());
            EXPECT_EQ(*descrip.scheduling->nice, 5);
        } else {
            EXPECT_FALSE(descrip.scheduling.has_value());
        }
    }
}
