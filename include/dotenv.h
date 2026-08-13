#ifndef DOTENV_H
#define DOTENV_H

/*
 * dotenv.h — optional KEY=VALUE config file, loaded into the process
 * environment at startup.
 *
 * This exists so GH_CLIENT_ID, TETRISD_HOST, DB_PATH, etc. can live in a
 * file instead of being exported by hand in every shell. It is NOT a shell:
 * no variable expansion, no command substitution, no multi-line values.
 *
 * Supported per line:
 *   KEY=value
 *   export KEY=value          (the "export " prefix is stripped)
 *   KEY="value with spaces"   (matching single or double quotes are stripped)
 *   # a comment, or a blank line (ignored)
 *
 * Variables already present in the environment are NEVER overwritten --
 * `export GH_CLIENT_ID=xyz; ./mini-gh-tracker` still wins over whatever is
 * in the file. This matches how every other dotenv loader behaves: the
 * file supplies defaults, the shell overrides them.
 *
 * If the file does not exist, this is a silent no-op -- a .env file is
 * always optional, never required.
 */
void dotenv_load(const char *path);

#endif
