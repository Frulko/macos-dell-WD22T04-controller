#include "dock.h"
#include <stdio.h>
int main(void) {
    dock_info info; dock_error error = {{0}};
    if (dock_read_info(&info, &error)) { fprintf(stderr, "%s\n", error.message); return 1; }
    printf("%s, bloc déclaré %u W, %zu composants\n", info.model, info.supply_watts, info.component_count);
    return 0;
}
