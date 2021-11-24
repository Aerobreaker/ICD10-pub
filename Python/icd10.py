"""
Download the latest ICD10 codes from CMS.gov and prepare them for uploading to
salesforce.
"""
from urllib import request
import zipfile
from datetime import datetime
import os
from os import path
from contextlib import suppress


CMS_URL = 'https://www.cms.gov'
ICD10_URL = CMS_URL + '/medicare/coding/icd10'


def extract_from_zip_by_name(filename, open_name):
    """
    Search for a specific file name in a zip and return the location.
    """
    if not zipfile.is_zipfile(filename):
        raise RuntimeError(filename+' is not a zip file!')
    output = []

    def open_recurse(zip_path):
        # This is to recurse through subdirectories if needed
        # Only one subdirectory is expected but this will handle everything
        # even if that changes
        if zip_path.is_dir():
            for child in zip_path.iterdir():
                open_recurse(child)
            return
        if zip_path.is_file() and open_name in zip_path.name:
            output.append(zip_path.at)

    with zipfile.ZipFile(filename) as fil:
        open_recurse(zipfile.Path(fil))
    if len(output) == 0:
        print('No files found matching "' + open_name + '"!')
    return tuple(output)


def get_most_recent_code_link():
    """
    Get the link to the most recent CM ICD10 codes from the CMS website.
    """
    print('Loading Centers for Medicare & Medicaid Services website...')
    with request.urlopen(ICD10_URL) as web_page:
        page = str(web_page.read())
    # The menu should be the first unordered list
    menu_st = page.find('<ul class="menu">')
    if menu_st < 0:
        raise RuntimeError('Cannot find menu of links!')
    menu_ed = page.find('</ul>', menu_st)
    page = page[menu_st:menu_ed+5]

    num_items = page.count('</li>')
    last = 0
    print('Locating most recent ICD-10 CM codes page...')
    for _ in range(num_items):
        # Each list item will have one link
        item_st = page.find('<li ', last)
        item_ed = page.find('</li>', item_st)
        link_st = page.find('<a href=', item_st, item_ed) + 9
        link_ed = page.find('"', link_st, item_ed)
        # This will be just the link for the item
        link = page[link_st:link_ed]
        item_st = page.find('>', link_ed, item_ed) + 1
        item_ed = page.find('<', item_st, item_ed)
        # This will be the text displayed for the link
        text = page[item_st:item_ed]
        if 'ICD-10' in text and 'CM' in text:
            # The link is a relative link so prepend the CMS url
            out_link = CMS_URL + link
            year = ' '.join(text.split('-')).split()[0]
            print(f'Found link for {year} ICD-10 codes: {out_link}')
            break
        last = item_ed
    else:
        raise RuntimeError('No ICD-10 CM links found!')

    return out_link, year


def get_tab_order_zip(link, year):
    """
    Get the link to the .zip file for the tabular order format codes.
    """
    print('Finding link for tabular order codes...')
    zip_file_name = year + '-code-descriptions-tabular-order.zip'
    with request.urlopen(link) as web_page:
        page = str(web_page.read())
    # There should be exactly one link on the page
    # with the words tabular order in it
    variants = ('Tabular Order',
                'tabular order',
                'Tabular order',
                'tabular Order')
    for variant in variants:
        marker = page.find(variant)
        if marker > 0:
            break
    else:
        raise RuntimeError('Cannot find link for tabular order zip!')
    # Find the last "> before the words "tabular order"
    link_ed = page.rfind('">', 0, marker)
    # Find the last " before the previously found ">
    link_st = page.rfind('"', 0, link_ed)+1
    # Between those quotes is the link to the zip for the tabular order files
    link = CMS_URL + page[link_st:link_ed]
    print('Downloading zip file...')
    with request.urlopen(link) as web_page:
        zip_file = web_page.read()
    with open(zip_file_name, 'wb') as fil:
        fil.write(zip_file)
    if not zipfile.is_zipfile(zip_file_name):
        raise RuntimeError('Encountered a problem getting '+zip_file_name+'!')
    return zip_file_name


def unzip_order_file(zip_file_name, year):
    """
    Locate and unzip just the order file from the CMS
    """
    # The file with the codes is called icd10cm_order_yyyy.txt

    ############################################################################
    # Meta-comment:                                                            #
    # icd10cm_codes_yyyy.txt would probably be better to use but the procedure #
    # says to use icd10cm_order and I haven't verified that there are no major #
    # differences                                                              #
    ############################################################################
    search_file_name = 'icd10cm_order_' + year + '.txt'
    print('Unzipping orders file...')
    order_file = extract_from_zip_by_name(zip_file_name, search_file_name)
    if len(order_file) != 1:
        raise RuntimeError('Multiple order files found!')
    with zipfile.ZipFile(zip_file_name) as zip_file:
        return zip_file.extract(order_file[0])


def parse_icd10_codes(text_file_name):
    """
    Read the ICD10 codes from CMS and parse them for building .go files.
    """
    # ICD10 file specs:
    # Columns   0-4: Sequence number
    # Columns  6-12: Code(non-decimal)
    # Columns    14: HIPAA covered? (0/1)
    # Columns 16-75: Short description
    # Columns   77+: Long description
    print('Parsing codes...')
    codes = []
    with open(text_file_name, encoding='UTF-8') as fil:
        for line in fil.readlines():
            # Don't keep codes that aren't covered by HIPAA
            if line[14] == '1':
                code = line[6:13].strip()
                long = line[77:]
                # We only use the code and the long description
                codes.append((code, long))
    print('Removing order codes file...')
    # Cleanup the extracted file
    os.remove(text_file_name)
    # Try to cleanup the directory the file was in
    # It'll error if the directory isn't empty
    # Ignore that and silently fail to remove the non-empty directory
    with suppress(OSError):
        os.rmdir(path.dirname(text_file_name))
    codes.sort()
    return codes


def build_go_files(file_name_base, year, codes):
    """
    Take the parsed codes and use them to build the .go files.
    """
    # .go file file specs:
    ############################################################################
    # Meta-comment:                                                            #
    # The first two lines are Intersystems Cache standard                      #
    ############################################################################
    #  Line 1: ~Format=5.S~
    #  Line 2: dd mmm yyyy   H:MM P   Cache
    #     where: d    = zero-padded day
    #            mmm  = short month name
    #            yyyy = year
    #            H    = non-zero-padded hour (12-hr format)
    #            MM   = zero-padded minute
    #            P    = AM/PM
    ############################################################################
    # Meta-comment:                                                            #
    # To protect potentially proprietary information, both the global name and #
    # the subscripts have been modified                                        #
    ############################################################################
    #  Line 3: ^NONDECGBL("Subscript 1") (or ^DECGBL for decimal version file)
    #  Line 4: dj_PLACEHOLDER FOR YEAR yyyy
    #     where: dj   = sunquest julian date (0 date changed in public version)
    #            yyyy = year
    # Line 5+: See below
    #   Odd lines: ^NONDECGBL("Subscript 1","{code}")
    #              (^DECGBL("Subscript 1","{code}") for decimal version codes)
    #  Even lines: {long description}
    print('Building global output files...')
    file_names = ('Non-decimal '+file_name_base+year+'.go',
                  'Decimal '+file_name_base+year+'.go',
                  'Combined '+file_name_base+year+'.go')
    print('  Building non-decimal file...')
    cur_datetime = datetime.now()
    cur_time = '{d.day} {d:%b} {d.year}'.format(d=cur_datetime)
    cur_time += '   {d.hour}:{d:%M} {d:%p}'.format(d=datetime.now())
    ############################################################################
    # Meta-comment:                                                            #
    # To protect potentially proprietary information, the 0 date for internal  #
    # julian date has been modified                                            #
    ############################################################################
    cur_dj = datetime.now().toordinal() - 726345
    with open(file_names[0], 'w', encoding='UTF-8') as fil:
        fil.write('~Format=5.S~\n')
        fil.write(cur_time + '   Cache\n')
        fil.write('^NONDECGBL("Subscript 1")\n')
        fil.write(f'{cur_dj}_PLACEHOLDER FOR YEAR {year}\n')
        for code, desc in codes:
            fil.write('^NONDECGBL("Subscript 1","'+code+'")\n')
            fil.write(desc)
        fil.write('\n\n')
    print('  Building decimal file...')
    with open(file_names[1], 'w', encoding='UTF-8') as fil:
        fil.write('~Format=5.S~\n')
        fil.write(cur_time + '   Cache\n')
        fil.write('^DECGBL("Subscript 1")\n')
        fil.write(f'{cur_dj}_PLACEHOLDER FOR YEAR {year}\n')
        for code, desc in codes:
            code = code if len(code) < 4 else code[:3]+'.'+code[3:]
            fil.write('^DECGBL("Subscript 1","'+code+'")\n')
            fil.write(desc)
        fil.write('\n\n')
    print('  Building combined file...')
    with open(file_names[2], 'w', encoding='UTF-8') as fil:
        fil.write('~Format=5.S~\n')
        fil.write(cur_time + '   Cache\n')
        fil.write('^NONDECGBL("Subscript 1")\n')
        fil.write(f'{cur_dj}_PLACEHOLDER FOR YEAR {year}\n')
        for code, desc in codes:
            fil.write('^NONDECGBL("Subscript 1","'+code+'")\n')
            fil.write(desc)
        # Combined file has non-decimal followed by decimal codes
        # with no header or anything in between
        for code, desc in codes:
            code = code if len(code) < 4 else code[:3]+'.'+code[3:]
            fil.write('^DECGBL("Subscript 1","'+code+'")\n')
            fil.write(desc)
        fil.write('\n\n')
    return file_names


def zip_go_files(go_file_names):
    """
    Zip up the .go files for uploading to salesforce.
    """
    print('Zipping files...')
    for go_name in go_file_names:
        zip_name = go_name[:-3]+'.zip'
        # zipfile.ZIP_DEFLATED is the default zip compression algorithm
        with zipfile.ZipFile(zip_name, 'w', zipfile.ZIP_DEFLATED) as fil:
            fil.write(go_name)


def cleanup_files(go_file_names):
    """
    Clean up the intermediary .go files.
    """
    print('Cleaning up intermediary files...')
    # The downloaded zip file will still be there
    # This is for verification that the script worked, if needed
    for go_file in go_file_names:
        os.remove(go_file)


def main(cleanup=False):
    """
    Main file.  Does all the work, with no cleanup by default.

    link, year = get_most_recent_code_link()
    zip_file_name = get_tab_order_zip(link, year)
    text_file_name = unzip_order_file(zip_file_name, year)
    codes = parse_icd10_codes(text_file_name)
    go_names = build_go_files('version - Filename_Base_',
                              year,
                              codes)
    zip_go_files(go_names)
    if cleanup, cleanup_files(go_names)
    """
    ############################################################################
    # Meta-comment:                                                            #
    # To protect potentially proprietary information, filenames have been      #
    # modified such that they don't match business-specific filenames          #
    ############################################################################
    # Assume the intermediary files are to be left behind
    link, year = get_most_recent_code_link()
    zip_file_name = get_tab_order_zip(link, year)
    text_file_name = unzip_order_file(zip_file_name, year)
    codes = parse_icd10_codes(text_file_name)
    go_names = build_go_files('version - Filename_Base_',
                              year,
                              codes)
    zip_go_files(go_names)
    if cleanup:
        cleanup_files(go_names)


if __name__ == '__main__':
    # If run by itself (not imported), go ahead and clean up
    # the intermediary files
    main(True)
