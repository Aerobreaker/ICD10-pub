"""
Test the runtimes of different variants of the ICD-10 loader
"""
import os
import os.path
import timeit

def main(repetitions=5):
    """
    Main functionality to run the tests.
    """

    def report(name, cmd, setup='pass'):
        print(name+':')
        timer = timeit.Timer(cmd, setup)
        loops, tim = timer.autorange()
        trials = [tim]
        trials.extend(timer.repeat(repetitions-1, loops))
        tim = min(trials)
        if tim > 0.01:
            times = f'{tim:f} seconds'
        else:
            times = f'{tim*1000:f} milliseconds'
        print(f'{loops} loops, best of {repetitions}: {times} per loop')

    # Run it once without reporting timing to download the zip file
    # This way variances due to network lag can be eliminated
    os.system('.\\x64\\Release\\ICD10.exe /q')

    report('Python version',
           'os.system("py "".\\\\Python\\\\icd10.py"" >nul")',
           'import os')
    report('C++ (x86) version',
           'os.system(".\\\\x86\\\\Release\\\\ICD10.exe >nul")',
           'import os')
    report('C++ (x64) version',
           'os.system(".\\\\x64\\\\Release\\\\ICD10.exe >nul")',
           'import os')
    report('C++ (x64) version (quiet)',
           'os.system(".\\\\x64\\\\Release\\\\ICD10.exe /q")',
           'import os')


def cleanup(skip=None):
    """
    Clean up the files left over from the tests
    """
    if skip is None:
        skip = {}
    for pth in os.listdir():
        if os.path.isfile(pth) and os.path.splitext(pth)[1] == '.zip':
            if pth not in skip:
                print(f'Removing "{pth}"...')
                os.remove(pth)


if __name__ == '__main__':
    print('')
    main()
    print('\n')
    cleanup()
