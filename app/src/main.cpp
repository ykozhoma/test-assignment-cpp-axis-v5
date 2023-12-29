#include <iomanip>
#include <iostream>

#include "imagecarver.h"

int main(int argc, char** argv) try
{
    whykozhoma::ImageCarver carver(argv[1]);
    carver.GetImageFromCamera();
    carver.SendXMLViaHttpPost();

    return 0;
}
catch(const std::exception& e)
{
    std::cerr << e.what() << std::endl;
}
