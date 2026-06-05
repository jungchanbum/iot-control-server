#ifndef COMMAND_H
#define COMMAND_H

/*
 * dispatch_command - Parses a null-terminated command string (newline stripped)
 *                   and routes it to the appropriate device handler.
 */
void dispatch_command(const char *buf);

#endif /* COMMAND_H */
