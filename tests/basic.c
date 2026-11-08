#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SPDA_IMPLEMENTATION
#include "../spda.h"

typedef struct 
{
    double x;
    double y;
    double z;
} Point3D;

typedef struct
{
    double r;
    double g;
    double b;
    double a;
} Color;

void printPoint3D(void *p)
{       
    /* Printer funciton for Point3D struct */
    printf("Point3D(%.lf, %.lf, %.lf)\n", ((Point3D *)p)->x, ((Point3D *)p)->y, ((Point3D *)p)->z);
}

void printColor(void *c)
{
    /* Printer funciton for Color struct */
    printf("Color(%.lf, %.lf, %.lf, %.lf)\n", ((Color *)c)->r, ((Color *)c)->g, ((Color *)c)->b, ((Color *)c)->a);
}

int main(void)
{   
    // Initializing the arrays
    Point3D *points = spda_create(Point3D);
    Color *colors = spda_create(Color);

    // Print array metadata like length, capacity & stride
    spda_print_metadata(points);
    spda_print_metadata(colors);
    
    // Initialize some points
    Point3D pa = {1, 3, 4};
    Point3D pb = {2, 4, 4};
    Point3D pc = {8, 1, 9};
    // Point3D ps[] = {pa, pb, pc};        // array of points

    // NOTE: ARRAY_LEN(x): finds the length of vanilla c array 'x'
    // Append the items
    // for (size_t i = 0; i < ARRAY_LEN(ps); ++i)         spda_append(points, ps[i % 3]);
    spda_append_many(points, pa, pb, pc, pa, pb, pc);

    Color ca = {123, 105, 97, 69};
    Color cb = {12, 11, 18, 143};
    Color cc = {90, 10, 98, 42};
    // Color cs[] = {ca, cb, cc};           // array of colors 
    
    // for (size_t i = 0; i < ARRAY_LEN(cs); ++i)         spda_append(colors, cs[i % 3]);
    spda_append_many(colors, ca, cb, cc, ca, cb, cc);

    // Print the items
    printf("\nPoints: \n");
    spda_print(points, printPoint3D);
    printf("\nColors: \n");
    spda_print(colors, printColor);

    printf("\n");
    spda_print_metadata(points);
    spda_print_metadata(colors);

    // Copy the array
    printf("Original Arrays: \n");
    spda_print(points, printPoint3D);
    spda_print(colors, printColor);

    // src 
    void *cpoints = spda_copy(points);
    void *ccolors = spda_copy(colors);

    printf("Copied Arrays: \n");
    spda_print(cpoints, printPoint3D);
    spda_print(ccolors, printColor);

    // Free the array 
    spda_destroy(points);
    spda_destroy(colors);
    return 0;
}
