struct Grid {
    int cells[3][2];
    unsigned char bytes[2][4][2];
};

int read_grid(struct Grid *grid, int row, int column) {
    return grid->cells[row][column] + grid->bytes[1][3][1];
}
