/* Invalid: a prior complete bound cannot be changed by a later definition. */
static int conflicting_array[2];
static int conflicting_array[3] = {11, 22, 33};
