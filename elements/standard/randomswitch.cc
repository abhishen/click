// -*- c-basic-offset: 4 -*-
/*
 * randomswitch.{cc,hh} -- randomizing switch element
 * Eddie Kohler
 *
 * Copyright (c) 2002 International Computer Science Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "randomswitch.hh"

RandomSwitch::RandomSwitch()
{
    MOD_INC_USE_COUNT;
    add_input();
}

RandomSwitch::~RandomSwitch()
{
    MOD_DEC_USE_COUNT;
}

void
RandomSwitch::notify_noutputs(int i)
{
    set_noutputs(i < 1 ? 0 : i);
}

void
RandomSwitch::push(int, Packet *p)
{
    int o = (random() % noutputs());
    output(o).push(p);
}

EXPORT_ELEMENT(RandomSwitch)
ELEMENT_MT_SAFE(RandomSwitch)

