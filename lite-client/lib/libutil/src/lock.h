/* Copyright (c) 2013-2019 GomSpace A/S. All rights reserved. */
/**
   @file

   Basic/core locking.

   Use for rare read/write locking, e.g. protecting register/de-regsiter functions.
*/

#include <gs/util/error.h>

gs_error_t gs_lock_init(void);
gs_error_t gs_lock_lock(void);
gs_error_t gs_lock_unlock(void);
