/** Fetch button definitions as JSON over HTTP and expose them to the UI. */
#pragma once

#define BUTTONS_MAX 16
#define BUTTON_LABEL_MAX 48
#define BUTTON_ID_MAX 48

typedef struct {
  char label[BUTTON_LABEL_MAX];
  char id[BUTTON_ID_MAX];
} button_data_t;

/*
 * POST a test array of {label, id} to an HTTP echo endpoint, read the
 * response, and parse the echoed array into `out`. Returns the number of
 * buttons parsed (<= max), or a negative value on error.
 */
int buttons_fetch(button_data_t* out, int max);
