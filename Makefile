ngram_grep: ngram_grep_cxx.cpp
	g++ -O3 -o ngram_grep -fopenmp -Wall $< -lboost_iostreams -lpcre