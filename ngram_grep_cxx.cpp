#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <functional>
#include <ext/hash_map>
#include <boost/iostreams/filter/bzip2.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <omp.h>
#include "pcre-cxx.h"


#define BUF_SIZE 1024
namespace BI = boost::iostreams;

struct ngram_info {
  const char *path_template;
  int n_files;
};

#define LANGUAGE_DE 0
#define LANGUAGE_EN 1

ngram_info ngram_paths_de[]={
  {"/export/local/yannick/ngrams/GERMAN/1gms/vocab.bz2",1},
  {"/export/local/yannick/ngrams/GERMAN/2gms/2gm-%04d.bz2",9},
  {"/export/local/yannick/ngrams/GERMAN/3gms/3gm-%04d.bz2",16},
  {"/export/local/yannick/ngrams/GERMAN/4gms/4gm-%04d.bz2",15},
  {"/export/local/yannick/ngrams/GERMAN/5gms/5gm-%04d.bz2",11},
};

ngram_info ngram_paths_en[]={
  {"/export/local/yannick/ngrams/EN/1gms/vocab.gz",1},
  {"/export/local/yannick/ngrams/EN/2gms/2gm-%04d.gz",31},
  {"/export/local/yannick/ngrams/EN/3gms/3gm-%04d.gz",97},
  {"/export/local/yannick/ngrams/EN/4gms/4gm-%04d.gz",131},
  {"/export/local/yannick/ngrams/EN/5gms/5gm-%04d.gz",117},
};

ngram_info *ngram_paths_all[]={
  ngram_paths_de,
  ngram_paths_en};

class IWordFilter
{
public:
  virtual const char *want_word(const char * w)=0;
  virtual ~IWordFilter() {};
};

class NullWordFilter: public IWordFilter
{
public:
  NullWordFilter() {}
  virtual const char* want_word(const char *w)
  {
    return w;
  }
  virtual ~NullWordFilter() {}
};

class RegexWordFilter: public IWordFilter
{
private:
  RegEx re;
public:
  RegexWordFilter(const char *re_str): re(re_str)
  { }
  virtual const char *want_word(const char *w) {
    if (re.Search(w,-1,PCRE_ANCHORED)) {
      return w;
    } else {
      return NULL;
    }
  }
  virtual ~RegexWordFilter() {
  }
};

struct eqstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1,s2)==0;
  }
};
  

class HashWordFilter: public IWordFilter
{
protected:
  typedef __gnu_cxx::hash_map<const char *, const char*, __gnu_cxx::hash<const char *>, eqstr> map_t;
  map_t mapping;
public:
  HashWordFilter(std::string prefix) {
    std::string dict_fname=prefix+".txt";
    char buf[BUF_SIZE];
    fprintf(stderr, "reading %s\n", dict_fname.c_str());
    std::ifstream myfile(dict_fname.c_str(), std::ios::binary|std::ios::in);
    char *w0, *w1;
    while (!myfile.eof()) {
      myfile.getline(buf,BUF_SIZE);
      w0=strtok(buf, " \t");
      if (w0!=NULL) {
	w1=strtok(NULL, " \t");
	if (w1==NULL) {
	  fprintf(stderr,"w1=null with w0=%s\n",w0);
	  continue;
	}
	mapping[strdup(w0)]=strdup(w1);
      }
    }
  }
  virtual const char *want_word(const char *w) {
    map_t::iterator it=mapping.find(w);
    if (it==mapping.end()) {
      return NULL;
    } else {
      return it->second;
      //return w;
    }
  }

  virtual ~HashWordFilter() {
  }
  };

class FilterEngine {
protected:
  std::vector<IWordFilter *> filters;
  int language;
  int want_output;
public:
  int pattern_len() {
    return filters.size();
  }

  int get_language() {
    return language;
  }
  
  FilterEngine(int argc, char **argv)
  {
    int opt_count=0;
    want_output=0;
    language=0;
    for (int i=0; i<argc; i++) {
      char *arg=argv[i];
      if (arg[0]=='-') {
        if (strcmp(arg+1,"EN")==0) {
          language=LANGUAGE_EN;
        };
        opt_count+=1;
      } else if (arg[0]=='@') {
        std::string prefix(arg+1);
        filters.push_back(new HashWordFilter(prefix));
        want_output |= (1<<(i-opt_count));
      } else if (arg[0]=='*') {
        filters.push_back(new NullWordFilter());
      } else if (arg[0]=='?') {
        filters.push_back(new NullWordFilter());
        want_output |= (1<<(i-opt_count));
      } else if (arg[0]=='%') {
        filters.push_back(new RegexWordFilter(arg+1));
      } else {
        filters.push_back(new RegexWordFilter(arg));
        want_output |= (1<<(i-opt_count));
      }
    }
  }

  void do_filtering(char *filename) {
    char buf[BUF_SIZE];
    char out_buf[2*BUF_SIZE];
    char *out_ptr=out_buf;
    const char *toks[16];
    std::ifstream myfile(filename, std::ios::binary|std::ios::in);
    BI::filtering_stream<BI::input> my_filter;
    if (strstr(filename,".bz2")!=NULL) {
      my_filter.push(BI::bzip2_decompressor());
    } else if (strstr(filename,".gz")!=NULL) {
      my_filter.push(BI::gzip_decompressor());
    }
    my_filter.push(myfile);
    
    fprintf(stderr, "Open %s\n",filename);
    while(!my_filter.eof()) {
      char *w0;
      char *saveptr;
      size_t i, n_toks=0;
      bool wanted=true;
      my_filter.getline(buf,BUF_SIZE);
      w0=strtok_r(buf, " \t", &saveptr);
      if (w0==NULL) {
	continue;
      }
      for (i=0; i<filters.size(); i++) {
	IWordFilter& filt=*filters[i];
	const char *w=filt.want_word(w0);
	if (w==NULL) {
	  //fprintf(stderr,"rejected for i=%d\n",i);
	  wanted=false;
	  break;
	}
	if ((want_output & (1<<i))!=0) {
	  toks[n_toks++]=w;
	}
	w0=strtok_r(NULL, " \t", &saveptr);
      }
      if (wanted) {
	while (w0!=NULL) {
	  toks[n_toks++]=w0;
	  w0=strtok_r(NULL, " \t", &saveptr);
	}
	// we do our own buffering so that there is no MT problem
	if ((out_ptr-out_buf)>BUF_SIZE) {
#pragma omp critical (writeout)
	  write(1, out_buf, (out_ptr-out_buf));

	  out_ptr=out_buf;
	}
	strcpy(out_ptr, toks[0]);
	out_ptr+=strlen(toks[0]);
	for (i=1; i<n_toks; i++) {
	  *out_ptr++=' ';
	  strcpy(out_ptr, toks[i]);
	  out_ptr+=strlen(toks[i]);
	}
	*out_ptr++='\n';
	//fprintf(stderr, "buffer: %ld chars\n",(out_ptr-out_buf));
      }
    }
    if (out_ptr!=out_buf) {
#pragma omp critical (writeout)
      write(1, out_buf, (out_ptr-out_buf));
    }
  }
};

int main(int argc, char **argv)
{
  FilterEngine engine(argc-1,argv+1);
  ngram_info &info=ngram_paths_all[engine.get_language()][engine.pattern_len()-1];
  omp_set_num_threads(3);
#pragma omp parallel for
  for (int i=0; i<info.n_files; i++) {
    char buf[BUF_SIZE];
    sprintf(buf, info.path_template,i);
    engine.do_filtering(buf);
  }
}
