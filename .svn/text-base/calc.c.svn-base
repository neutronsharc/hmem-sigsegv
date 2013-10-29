
#include "stdio.h"


int main(int argc, char** argv)
{

    double ha[] = {1.00, 0.99, 0.95, 0.9, 0.76, 0.5, 0.33 };
    int hlen=7;
 
    double qa[] = {500, 1000, 5000, 8250, 11000};
    int qlen=5;

    //double ta[] = {10, 60, 40, 150};
    double ta[] = {10, 60, 40, 150};
    int tlen=4;

    double h, q, t;
    double r=70, w=70;

    int i,j,k;
    double  base=0.0;
    double lat1, lat2;

    for(j=0; j<qlen; j++) // db query
    {
        for(k=0; k<hlen; k++) // hit ratio
        {
            t = ta[0];  q = qa[j]; h=ha[k];
            base = (1-h)*(q+w) + 3*t + h*r - 2*h*t;

            for(i=0; i<tlen; i++) // network latency
            {
                t = ta[i];  q = qa[j]; h=ha[k];

                lat1 = (1-h)*q + 3*t - 2*h*t; // basic
                lat2 = (1-h)*(q+w) + 3*t + h*r - 2*h*t; // hybrid
                printf("network-lat=%.0f,  q=%.0f,   hitrate=%.2f:  ", t, q, h);
                if( h<0.4 )
                    printf("Basic-lat=%.0f, Hybrid-lat=%.0f, norm=%.2f\n", lat1, lat2, lat2/base);
                else
                    printf("\t  Hybrid-lat=%.0f, norm=%.2f\n", lat2, lat2/base);
            }
            printf("\n");

        }
    }

}
