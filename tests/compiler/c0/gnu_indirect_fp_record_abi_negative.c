typedef struct {
    double value;
} FloatingRecord;

double unsupported_sysv_fp_record_return(double value,
                                         FloatingRecord (*reader)(double)) {
    FloatingRecord result = reader(value);
    return result.value;
}
