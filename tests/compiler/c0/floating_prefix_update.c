double decrement_timeout(double timeout)
{
    --timeout;
    return timeout;
}

float increment_ratio(float ratio)
{
    ++ratio;
    return ratio;
}

double postfix_decrement_timeout(double timeout)
{
    return timeout--;
}

float postfix_increment_ratio(float ratio)
{
    return ratio++;
}
