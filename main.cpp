#include "main.h"
int main(int argc, char** argv){
    std::cout<<"hello world"<<std::endl;
    if(argc<2){
        std::cout<<argv[0]<<"Version"<<tgbuntu_VERSION_MAJOR<<"."<<tgbuntu_VERSION_MINOR<<std::endl;
        std::cout<<"Usage: "<<argv[0]<< "number"<<std::endl;
    }
}