cmake_minimum_required(VERSION 3.21)
project(ReSID)

set(CMAKE_CXX_STANDARD 17)



add_library(ReSID 
            dac.cc
            envelope.cc
            extfilt.cc
            filter.cc
            pot.cc
            sid.cc
            version.cc
            voice.cc
            wave.cc)
