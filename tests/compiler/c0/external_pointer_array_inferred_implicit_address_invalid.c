/* The inferred-bound entry path must reject implicit object addresses too. */
int external_pointer_array_inferred_scalar;
int *external_pointer_array_inferred_bad[] = {external_pointer_array_inferred_scalar};
