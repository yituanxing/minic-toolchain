struct Packet { int bytes; };
/* A function-pointer field must still have a valid parameter declaration. */
struct BadRing { struct Packet *(*deliver)(int,); };
