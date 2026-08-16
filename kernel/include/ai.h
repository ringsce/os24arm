/**
 * @file ai.h
 * @brief On-box "AI" assistant command
 *
 * Not a real LLM - there is no network stack in this kernel to reach one.
 * This is a small heuristic assistant that watches which built-in commands
 * get typed at the shell during the current session and uses that to
 * surface usage stats and suggest commands the user hasn't tried yet.
 * State lives in RAM only: the ext4/btrfs drivers in this build are
 * read-only (see fs/ext4/ext4.c, fs/btrfs/btrfs.c), so there's nowhere
 * durable to persist it across reboots yet.
 */

#ifndef AI_H
#define AI_H

/**
 * @brief Note that a command was typed at the shell, for AI STATS/TIP.
 *
 * Safe to call for every command line the shell dispatches, recognized or
 * not - names that aren't in the AI's known-command list are ignored.
 *
 * @param[in] name  Command name (argv[0]), already upper-cased.
 */
void ai_record_command(const char *name);

/**
 * @brief Handle the AI command (AI, AI STATUS, AI STATS, AI TIP, AI HELP).
 */
void ai_run(int argc, char **argv);

#endif /* AI_H */
