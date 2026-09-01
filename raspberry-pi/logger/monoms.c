/* monoms - print CLOCK_MONOTONIC in milliseconds and exit.
 *
 * Shell has no built-in way to read CLOCK_MONOTONIC (only `date`, which is
 * wall clock). session_start.sh and session_stop.sh need this exact clock -
 * the same one csi-logger stamps every CSI line with - to compute
 * t0_monotonic_ms and the camera PTS anchor. See docs/RASPBERRY_PI_V1.md.
 */

#include <stdio.h>
#include <time.h>

int main(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("%lld\n", (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    return 0;
}
