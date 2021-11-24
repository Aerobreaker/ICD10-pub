# ICD10-pub
 Public version of ICD10

Timing tests:
```
Python version:
1 loops, best of 5: 2.096985 seconds per loop
C++ (x86) version:
1 loops, best of 5: 1.284485 seconds per loop
C++ (x64) version:
1 loops, best of 5: 1.214579 seconds per loop
C++ (x64) version (quiet):
1 loops, best of 5: 1.246002 seconds per loop
```

### FAQ
1. Was this necessary?
   - No.  Well, kind of yes but really no.
2. Was it worth it?
   - See the table below.  The python script will result in time savings relatively quickly.  The C++ program, while it won't be a significant time save compared to development effort (especially compared to the python script), did result in me learning more about how to use CURL and zip libraries in C++.
   - In summary, Yes it was indeed worth it.

| Task | Duration |
| :--: |   :--:   |
| Time to perform manually | ~20-30 minutes |
| Time to perform with python script | ~2 seconds |
| Time to perform with C++ program | ~1.2 seconds |
| Time savings using python script | ~99.8% |
| Time savings using C++ program | ~99.9% |
| Time savings using C++ program over python script | ~40% |
| Development time for python script | ~3 hours |
| Development time for C++ program | ~20 hours <br />(runtimes between debug and release were inconsistent, leading to unnecessary micro-optimization) |
| Break-even point for python script | ~7 years (run once anually) |
| Break-even point for C++ program | ~48 years (run once anually) |
| Break-even point for C++ program over python script | ~6000 years (run once anually) |
    
