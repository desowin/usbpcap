#ifndef GETOPT_H
#define GETOPT_H

extern char *optarg; /* Global argument pointer. */

int getopt(int argc, char *argv[], char *optstring);

#endif /* GETOPT_H */
