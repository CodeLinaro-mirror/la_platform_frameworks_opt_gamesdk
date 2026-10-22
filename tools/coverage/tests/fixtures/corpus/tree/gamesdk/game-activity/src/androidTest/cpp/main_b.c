int util_add(int a, int b);
int beta_value(int value);
int main(void) {
    return (util_add(1, 2) == 3 && beta_value(2) == 21) ? 0 : 1;
}
