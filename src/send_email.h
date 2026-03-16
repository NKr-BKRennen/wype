/*
 *  send_email.h: Send PDF certificate via email after wipe completion (BKR)
 *
 *  This program is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free Software
 *  Foundation, version 2.
 */

#ifndef SEND_EMAIL_H_
#define SEND_EMAIL_H_

#include "context.h"

/**
 * Send an email with the PDF certificate attached.
 * Reads SMTP settings from nwipe.conf (Email_Settings group).
 *
 * @param c  The device context (contains PDF_filename, device_model, device_serial_no)
 * @return 0 on success, -1 on failure or if email is disabled
 */
int nwipe_send_email( nwipe_context_t* c );

#endif /* SEND_EMAIL_H_ */
