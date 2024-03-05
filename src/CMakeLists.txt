add_executable(${PROJECT_NAME}
	main.cpp
)
target_link_libraries(${PROJECT_NAME}
	Freetype::Freetype
)

# install
install(TARGETS ${PROJECT_NAME})
