## Bugs Reporting

Report any bugs and dysfunctions to
[issues](<https://github.com/dsvi/MicroDict/issues>)

If a certain dictionary leads to problems, you **must** attach the dict files or provide download links in bug reports.

Attach your version info from the menu "About" --> "Copy version info".

## Logs

The quickest thing to attach to a report is the program's own log.

Start MicroDict from a terminal and keep the output, or start it with the
command line flag `--log-to-file`; that writes `microdict_log.txt` into the configuration
folder the next time the program runs.

## macOS Tips

macOS has a built-in crash reporter. Copy all the info from the bug report window.

Try to start the program from the command line and save the logs, as above.

## Linux Tips

If you have no clue, search and learn how to obtain coredumps using tools provided by your distro, and/or learn how to use a debugger. Now is the time.

Noticeably, in recent years, popular distros support new technologies such as `systemd-coredump` and [Debuginfod](https://sourceware.org/elfutils/Debuginfod.html). Traditional approaches include installing debug packages or building from source.

At least, you should start the program in the terminal and upload the log.

