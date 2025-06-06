#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Utils
{
    struct ExecResult
    {
        std::string StdOut;
        std::string StdErr;
        int ExitCode;
    };

    /// @brief Tokenizes a command into its arguments
    /// @param command command
    /// @return command arguments
    std::vector<std::string> TokenizeCommand(const std::string& command);

    /// @brief Executes a command
    /// @param cmd command
    /// @param bufferSize buffer size in bytes
    /// @return command output
    std::string PipeOpen(const std::string& cmd, const size_t bufferSize = 128);

    /// @brief Executes a command and captures its output.
    ///
    /// Runs a command without using a shell, capturing both StdOut and StdErr.
    /// The function waits for the process to finish before returning.
    ///
    /// @param cmd The command to execute (no shell features like piping or expansions).
    /// @return An optional ExecResult containing the command's StdOut, StdErr, and ExitCode.
    /// @throw boost::process::process_error if the command fails to start.
    std::optional<ExecResult> Exec(const std::string& cmd);
} // namespace Utils
