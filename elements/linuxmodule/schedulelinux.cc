/*
 * schedulelinux.{cc,hh} -- go back to linux scheduler
 *
 * Copyright (c) 1999 Massachusetts Institute of Technology.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "router.hh"
#include "schedulelinux.hh"
#include "confparse.hh"
#include "error.hh"
#include "glue.hh"
#include <unistd.h>

ScheduleLinux *
ScheduleLinux::clone() const
{
  return new ScheduleLinux();
}

int
ScheduleLinux::configure(const String &conf, ErrorHandler *errh)
{
  return cp_va_parse(conf, this, errh, cpEnd);
}

void
ScheduleLinux::run_scheduled()
{
  schedule();
  if (signal_pending(current)) {
    router()->please_stop_driver();
  }
  reschedule();
}

EXPORT_ELEMENT(ScheduleLinux)

