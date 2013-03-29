This is a low-tech alternative for pattern search in
Google's n-gram data, which does not require you to
decompress or index anything. It uses OpenMP and boost's
wrapper around zlib/bzlib to do this at a decent speed
on multiple cores.

The following kinds of patterns are supported:

   * @filename -- given a list of word-form to lemma map entries,
     matches any word form in that list (and outputs the lemma instead).
   * *, ? -- star matches any word and ignores it, question mark
     matches any word and outputs it.
   * %regex -- matches some regex and ignores that token
   * everything else -- is interpreted as a regex to match;
     the match results are part of the output.
