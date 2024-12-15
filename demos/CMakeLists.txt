add_executable(sslecho sslecho.cc)
target_link_libraries(sslecho crypto ssl)
