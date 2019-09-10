/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 */

#ifndef ATTENTIVE_AT_UNIX_H
#define ATTENTIVE_AT_UNIX_H

#include <at.h>

/**
 * Create an AT channel instance.
 *
 * @returns Instance pointer on success, NULL and sets errno on failure.
 */
struct at *at_alloc_unix(void);

#endif

/* vim: set ts=4 sw=4 et: */
