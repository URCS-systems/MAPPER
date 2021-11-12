/*
 * FILE: hrtimer_x86.h
 * DESCRIPTION: Hearder file for hrtimer_x86.c - a high-resolution timer on x86.
 */

#ifndef _HRTIMER_X86_H_
#define _HRTIMER_X86_H_

typedef long long hrtime_t;

/* get the number of CPU cycles since startup */
hrtime_t gethrcycle(void)
{
    unsigned int tmp[2];

    __asm__ ("rdtsc"
	     : "=a" (tmp[1]), "=d" (tmp[0])
	     : "c" (0x10) );

    return ( ((hrtime_t)tmp[0] << 32 | tmp[1]) );
}

/* get the number of CPU cycles per microsecond - from Linux /proc filesystem
 * return<0 on error
 */
double getMHZ(void)
{
    double mhz = -1;
    char line[1024], *s, search_str[] = "cpu MHz";
    FILE *fp;

    /* open proc/cpuinfo */
    if ((fp = fopen("/proc/cpuinfo", "r")) == NULL)
	return -1;

    /* ignore all lines until we reach MHz information */
    while (fgets(line, 1024, fp) != NULL) {
	if (strstr(line, search_str) != NULL) {
	    /* ignore all characters in line up to : */
	    for (s = line; *s && (*s != ':'); ++s);
	    /* get MHz number */
	    if (*s && (sscanf(s+1, "%lf", &mhz) == 1))
		break;
	}
    }

    if (fp!=NULL) fclose(fp);

    return mhz;
}

/* get the elapsed time (in seconds) since startup */
double gethrtime(void)
{
    static double CPU_MHZ=0;
    if (CPU_MHZ==0) CPU_MHZ=getMHZ();
    return (gethrcycle()*0.000001)/CPU_MHZ;
}

#endif	// _HRTIMER_X86_H_
