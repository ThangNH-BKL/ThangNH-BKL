#include <filesystem>
#include <iostream>
int main(){auto p=std::filesystem::u8path("D:/Project_Python/.venv/Copydata/UI_FTP_Demo/Source");std::cout<<p.string()<<" exists="<<std::filesystem::exists(p)<<" dir="<<std::filesystem::is_directory(p)<<"\n";}