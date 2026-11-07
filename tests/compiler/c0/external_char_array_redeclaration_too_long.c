/* A completed bound cannot be silently widened by a later string definition. */
char short_array[2];
char short_array[] = "abc";
