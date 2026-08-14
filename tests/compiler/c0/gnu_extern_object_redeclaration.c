extern int repeated_scalar;
extern int repeated_scalar;

extern const int repeated_const;
extern const int repeated_const;

extern int repeated_incomplete[];
extern int repeated_incomplete[];

extern int completed_array[];
extern int completed_array[4];
extern int completed_array[];

extern int fixed_array[3];
extern int fixed_array[3];
extern int fixed_array[];

struct node;
extern struct node repeated_record;
extern struct node repeated_record;

int defined_object = 7;
extern int defined_object;

int read_redeclarations(void)
{
    return repeated_scalar + repeated_const + completed_array[3] + fixed_array[2] +
           defined_object + (int)sizeof(completed_array) + (int)sizeof(fixed_array) +
           (repeated_incomplete != (void *)0);
}
