#ifndef PERSISTER_HH
#define PERSISTER_HH

#include <string>

class basic_persister {
public:
    basic_persister(std::string filename):filename_(filename){};
    virtual bool persist() = 0;
    virtual bool load() = 0;
    bool set_file(std::string filename){filename_ = filename;};
private:
    std::string filename_;
};


template<typename T>
class single_persister : public basic_persister{
public:
    single_persister(std::string filename):filename_(filename){};
    virtual bool set(const T& src) = 0;
    virtual bool update(const T& src) = 0;

    T data;
};



template<typename T>
class serial_persister : public basic_persister{
public:
    serial_persister(std::string filename):filename_(filename){};
    virtual bool append(const T& item) = 0;
    virtual bool remove(int start, int length=1) = 0;

    T data;
};





#endif