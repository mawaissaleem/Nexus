#include "nexus/core/query.hpp"
#include "nexus/providers/dangerous_command.hpp"
#include "nexus/providers/shell_provider.hpp"
#include <atomic>
#include <cassert>
#include <iostream>

using nexus::providers::is_dangerous_command;

int main() {
    const char* dangerous[] = {
        "rm -rf /",
        "rm -fr ~/x",
        "rm -r dir",
        "rm -f file",
        "rm -Rf dir",
        "rm file --recursive",
        "rm --force file",
        "rm -i -rf x",
        "/bin/rm -rf x",
        "dd if=/dev/zero of=/dev/sda",
        "mkfs /dev/sda1",
        "mkfs.ext4 /dev/sda1",
        "shred secret",
        "wipefs -a /dev/sda",
        "fdisk /dev/sda",
        "parted /dev/sda",
        "sfdisk /dev/sda",
        "shutdown now",
        "reboot",
        "poweroff",
        "halt",
        "systemctl poweroff",
        "systemctl reboot",
        "systemctl halt",
        "chmod -R 777 /",
        "chown -R me /",
        "echo hi > /dev/sda",
        "echo hi >/dev/nvme0n1",
        ":(){ :|:& };:",
        "sudo rm -rf /",
        "sudo -E rm -rf /",
        "FOO=1 rm -rf x",
        "ls; rm -rf x",
        "ls && rm -rf x",
        "ls || shutdown now",
        "ls | dd of=/dev/sda",
        "ls & reboot",
        "ls\nreboot",
        "(rm -rf x)",
        "echo $(rm -rf x)",
        "nohup shutdown now",
        "xargs rm -rf",
        "echo 'a b' ; 'rm' -rf x",
        "sh -c \"rm -rf /\"",
        "bash -c 'rm -rf /'",
        "bash -lc 'ls; reboot'",
        "sudo sh -c 'mkfs.ext4 /dev/sda1'",
        "sudo -u bob rm -rf /",
        "sudo -u root shutdown now",
        "doas -u bob reboot",
        "env -u FOO rm -rf x",
        "for f in *; do rm -rf \"$f\"; done",
        "if true; then reboot; fi",
        "{ rm -rf /; }",
        "! rm -rf x",
    };
    for (const char* cmd : dangerous) {
        assert(is_dangerous_command(cmd));
    }

    const char* harmless[] = {
        "",
        "ls -la",
        "rm file",
        "rm -i file",
        "rmdir dir",
        "echo rm -rf /",
        "echo \"rm -rf /\"",
        "grep dd file",
        "git add .",
        "ls | grep reboot",
        "systemctl status ssh",
        "systemctl restart ssh",
        "chmod -R 755 dir",
        "chmod 755 /",
        "echo hi > out.txt",
        "echo hi > /dev/null",
        "cat /dev/sda1",
        "echo shutdown",
        "man mkfs",
        "ls; echo done",
        "sudo apt update",
        "sudo",
        "FOO=1",
        "ddrescue a b",
        "sh -c \"echo hi\"",
        "bash script.sh",
        "sudo -u bob ls",
        "for f in *; do echo \"$f\"; done",
    };
    for (const char* cmd : harmless) {
        assert(!is_dangerous_command(cmd));
    }

    // The shell provider asks before a dangerous command and stays plain for a normal one.
    nexus::providers::ShellProvider provider;
    const std::atomic<bool> cancel{false};
    auto risky = provider.search(nexus::core::Query(">rm -rf /tmp/x", ">"), cancel);
    assert(risky.size() == 1 && risky[0].confirm);
    assert(risky[0].execution_payload == "rm -rf /tmp/x");
    auto plain = provider.search(nexus::core::Query(">ls -la", ">"), cancel);
    assert(plain.size() == 1 && !plain[0].confirm);

    std::cout << "All dangerous command tests passed successfully!\n";
    return 0;
}
