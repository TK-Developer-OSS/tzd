/* TzD driver specs.  .d files are TzD source.  */
  {".d", "@tzd", 0, 0, 0},
  {"@tzd",
     "%{E|M|MM:cc1tzd -E %(cpp_options) %(cpp_debug_options)}\
      %{!E:%{!M:%{!MM:\
	%{save-temps*|no-integrated-cpp:cc1tzd -E %(cpp_options) -o %{save-temps*:%b.i} %{!save-temps*:%g.i} \n\
	    cc1tzd -fpreprocessed %{save-temps*:%b.i} %{!save-temps*:%g.i} %(cc1_options)}\
	%{!save-temps*:%{!no-integrated-cpp:\
	    cc1tzd %(cpp_unique_options) %(cc1_options)}}\
        %{!fsyntax-only:%(invoke_as)}}}}", 0, 0, 0},
