/*
 * hw/clock: the local time shown in the status line, and setting the system clock from a local
 * SYSTEMTIME (local -> UTC through the time-zone bias).
 *
 * Types: SYSTEMTIME16 (clock.h); TIME_ZONE_INFORMATION and the kernel's TIME_FIELDS (local).
 * No globals. Setting the clock is verified by booting only.
 */
#include "app.h"

/* kernel TIME_FIELDS (RtlTimeFieldsToTime) */
typedef struct {
    u16 Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday;
} TIME_FIELDS;

/* FormatLocalTime (0x24E80): "DD.MM.YYYY - hh:mm:ss" of GetLocalTime into out; returns out. */
char *__cdecl FormatLocalTime(char *out)
{
    SYSTEMTIME16 st;
    orig_GetLocalTime((u16 *)&st);
    orig_sprintf(out, "%02u.%02u.%04u - %02u:%02u:%02u", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute,
                 st.wSecond);
    return out;
}

/* GetTimeZoneBias100ns (0x24F20): the current bias (Bias, plus StandardBias / DaylightBias by
 * the GetTimeZoneInformation result, 0 when that is invalid) in minutes, as 100 ns units. */
void __cdecl GetTimeZoneBias100ns(long long *out)
{
    TIME_ZONE_INFORMATION tz;
    int bias;
    switch (orig_GetTimeZoneInformation(&tz)) {
    case 0: bias = tz.Bias; break;
    case 1: bias = tz.Bias + tz.StandardBias; break;
    case 2: bias = tz.Bias + tz.DaylightBias; break;
    default: bias = 0; break;
    }
    *out = (long long)(int)(bias * 60) * 10000000;
}

/* SetLocalTimeFromSystemTime (0x25010): st (a SYSTEMTIME16) -> TIME_FIELDS -> time, plus the
 * time-zone bias -> NtSetSystemTime. Returns 0 if the fields are invalid, else the NTSTATUS.
 * QUIRK: TIME_FIELDS.Weekday is not set by the original (debug fill 0xCCCC). */
long __cdecl SetLocalTimeFromSystemTime(const u16 *st)
{
    const SYSTEMTIME16 *s = (const SYSTEMTIME16 *)st;
    long long bias, t, utc;
    TIME_FIELDS tf;
    GetTimeZoneBias100ns(&bias);
    tf.Year = s->wYear;
    tf.Month = s->wMonth;
    tf.Day = s->wDay;
    tf.Hour = s->wHour;
    tf.Minute = s->wMinute;
    tf.Second = s->wSecond;
    tf.Milliseconds = s->wMilliseconds;
    tf.Weekday = 0xCCCC;
    u8 ok = KCALL(RtlTimeFieldsToTime, u8(__stdcall *)(TIME_FIELDS *, long long *))(&tf, &t);
    if (!ok)
        return 0;
    utc = t + bias;
    return KCALL(NtSetSystemTime, long(__stdcall *)(long long *, long long *))(&utc, 0);
}

