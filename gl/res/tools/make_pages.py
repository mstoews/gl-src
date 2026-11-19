#!/usr/bin/env python3
"""make_pages.py - the list-and-detail pages' forms (spec #8 5; plan S8-M2).

Writes, from the PAGES table below:
  gl/res/pages.rc               the form templates (WS_CHILD | DS_CONTROL), #included by port.rc
  gl/port/pages/PageDescs.inc   the C++ descriptors RecordPage reads: the list's
                                columns, each field's control, key, kind and width,
                                and every control's anchor

Run it after changing a page:  python -I gl/res/tools/make_pages.py gl

The layout is fixed (dialog units, Segoe UI 9): New, Save and Delete (and a
page's own buttons) along the top; the filter and the list on the left
(x 4-180); the field groups in two columns of 200 from x 188 and 396, a row
every 14. A group goes in the column it names (1 or 2). An account field is
the account, its dept and the account's name beside them. Control IDs are
numbers, per page (each page is its own template): see PortIds.h, "pages".
"""
import os
import sys

# A field: (key, label, kind, width in characters, flags)
#   kinds: text upper number decimal amount date choice account password readonly
#   flags: 'k' the record's key (required; read-only once saved), 'r' required
REPS = lambda what: [
    (1, what, [('code', 'C&ode:', 'upper', 5, 'k'), ('shortName', 'S&hort name:', 'upper', 12, ''),
               ('name', 'N&ame:', 'text', 40, ''), ('territory', 'Te&rritory:', 'text', 10, '')]),
    (1, 'Address', [('address1', 'Street:', 'text', 30, ''), ('address2', 'City:', 'text', 30, ''),
                    ('address3', 'State/Country:', 'text', 30, ''), ('postal', 'Postal code:', 'upper', 7, ''),
                    ('phone', 'Phone:', 'text', 16, ''), ('fax', 'Fax:', 'text', 16, ''),
                    ('contact', 'Contact:', 'text', 12, '')]),
    (2, 'Terms', [('terms', '&Terms (days):', 'number', 3, ''), ('creditLimit', 'Credit &limit:', 'amount', 16, '')]),
]

PAGES = [
    dict(name='departments', idd='IDD_PG_DEPARTMENTS', title='Departments',
         columns=[('Dept', 48, 0), ('Description', 170, 0)],
         groups=[(1, 'Department', [('depart', '&Dept:', 'upper', 4, 'k'),
                                    ('descript', 'D&escription:', 'text', 40, 'r')])]),
    dict(name='medicalReps', idd='IDD_PG_MEDREPS', title='Medical Reps',
         columns=[('Code', 48, 0), ('Name', 170, 0)], groups=REPS('Medical rep')),
    dict(name='doctors', idd='IDD_PG_DOCTORS', title='Doctors',
         columns=[('Code', 48, 0), ('Name', 170, 0)], groups=REPS('Doctor')),
    dict(name='products', idd='IDD_PG_PRODUCTS', title='Products',
         columns=[('Code', 56, 0), ('Name', 160, 0)],
         groups=[(1, 'Product', [('code', 'C&ode:', 'upper', 7, 'k'), ('name', 'S&hort name:', 'text', 10, ''),
                                 ('fullName', 'N&ame:', 'text', 40, ''), ('type', 'T&ype:', 'upper', 2, ''),
                                 ('unit', '&Unit:', 'upper', 3, '')]),
                 (1, 'Prices', [('onHand', '&Cost:', 'decimal', 14, ''), ('avPrice', 'Unit &price:', 'decimal', 14, '')])]),
    dict(name='vendors', idd='IDD_PG_VENDORS', title='Vendors',
         columns=[('Vendor', 56, 0), ('Name', 160, 0)],
         groups=[(1, 'Vendor', [('number', 'N&umber:', 'upper', 6, 'k'), ('shortName', 'S&hort name:', 'upper', 12, 'r'),
                                ('name', 'N&ame:', 'text', 40, 'r'), ('type', 'T&ype:', 'choice', 1, 'r'),
                                ('vin', 'VAT No.:', 'text', 10, ''), ('medrep', 'Medical rep:', 'upper', 6, '')]),
                 (1, 'Address', [('address1', 'Street:', 'text', 30, ''), ('address2', 'City:', 'text', 30, ''),
                                 ('address3', 'State/Country:', 'text', 30, ''), ('postal', 'Postal code:', 'upper', 7, ''),
                                 ('phone', 'Phone:', 'text', 16, ''), ('fax', 'Fax:', 'text', 16, ''),
                                 ('contact', '&Contact:', 'text', 12, '')]),
                 (2, 'Terms and taxes', [('terms', '&Terms (days):', 'number', 3, ''), ('vatRate', '&VAT %:', 'number', 2, ''),
                                         ('whRate', '&W/H tax %:', 'number', 2, ''),
                                         ('discountDays', 'Discount days:', 'number', 2, ''),
                                         ('discountPercent', 'Discount %:', 'number', 2, '')]),
                 (2, 'Accounts', [('ap', 'AP:', 'account', 7, 'r'), ('vat', 'VAT:', 'account', 7, '')])]),
    dict(name='accounts', idd='IDD_PG_ACCOUNTS', title='Chart of Accounts',
         columns=[('Account', 52, 0), ('Dept', 32, 0), ('Description', 150, 0)],
         groups=[(1, 'Account', [('account', '&Account:', 'upper', 7, 'k'), ('dept', 'D&ept:', 'upper', 4, 'K'),
                                 ('descript', 'D&escription:', 'text', 45, 'r'),
                                 ('fdescript', '&Foreign desc.:', 'text', 45, ''), ('type', '&Type:', 'choice', 1, '')]),
                 (1, 'Balances', [('currBal', 'Balance:', 'amount', 16, ''), ('openBal', '&Opening:', 'amount', 16, '')])]),
    dict(name='assetTypes', idd='IDD_PG_ASSETTYPES', title='Asset Types',
         columns=[('Type', 40, 0), ('Description', 170, 0)],
         groups=[(1, 'Asset type', [('code', '&Type:', 'upper', 4, 'k'), ('description', 'D&escription:', 'text', 30, 'r')]),
                 (1, 'Accounts', [('asset', '&Asset:', 'account', 7, 'r'), ('expense', 'E&xpense:', 'account', 7, 'r'),
                                  ('accum', 'Acc&um. dep.:', 'account', 7, 'r')])]),
    dict(name='assets', idd='IDD_PG_ASSETS', title='Fixed Assets',
         columns=[('Asset', 56, 0), ('Description', 150, 0)], buttons=[('Dis&pose...', 'dispose')],
         groups=[(1, 'Asset', [('code', 'C&ode:', 'upper', 10, 'k'), ('type', 'T&ype:', 'choice', 4, 'r'),
                               ('description', 'D&escription:', 'text', 40, 'r'),
                               ('reference', '&Reference:', 'text', 40, ''),
                               ('datePurchased', 'P&urchased:', 'date', 8, 'r'),
                               ('dateSold', 'Disposed:', 'readonly', 10, '')]),
                 (2, 'Depreciation', [('method', '&Method:', 'choice', 1, ''), ('cost', '&Cost:', 'amount', 16, 'r'),
                                      ('salvage', '&Salvage:', 'amount', 16, ''),
                                      ('life', '&Life (months):', 'number', 4, 'r'),
                                      ('percent', 'R&ate %:', 'number', 2, ''),
                                      ('monthsUsed', 'Months used:', 'number', 4, ''),
                                      ('accumulated', 'Accumulated:', 'amount', 16, ''),
                                      ('monthly', 'Monthly:', 'readonly', 16, ''),
                                      ('bookValue', 'Book value:', 'readonly', 16, '')])]),
    dict(name='users', idd='IDD_PG_USERS', title='Users', commands=True,
         columns=[('User', 56, 0), ('Description', 150, 0)],
         groups=[(1, 'User', [('username', '&User:', 'upper', 10, 'k'), ('descript', 'D&escription:', 'text', 30, ''),
                              ('date', 'Da&te:', 'date', 8, ''), ('password', '&Password:', 'password', 10, ''),
                              ('level', '&Level:', 'upper', 1, 'r')])]),
]

KIND = {'text': 'Text', 'upper': 'Upper', 'number': 'Number', 'decimal': 'Decimal', 'amount': 'Amount',
        'date': 'Date', 'choice': 'Choice', 'account': 'Account', 'password': 'Password', 'readonly': 'ReadOnly'}

FILTER, LIST, COMMANDS, BUTTON0 = 21000, 21001, 21002, 21003
GROUP0, FIELD0, DEPT0, NAME0 = 21100, 21010, 21300, 21200
WIDTH, MIN_HEIGHT = 600, 330


def edit_width(kind, chars):
    if kind == 'number':
        return 32
    if kind in ('amount', 'decimal', 'readonly'):
        return 64
    if kind == 'date':
        return 64
    if kind == 'choice':
        return 100
    return max(20, min(128, chars * 4 + 8))


def build(page):
    rc, fields, anchors = [], [], []
    x = 4
    buttons = [('&New', 'IDC_PG_NEW'), ('&Save', 'IDC_PG_SAVE'), ('&Delete', 'IDC_PG_DELETE')]
    extra = []
    for i, (text, _) in enumerate(page.get('buttons', [])):
        buttons.append((text, str(BUTTON0 + i)))
        extra.append(BUTTON0 + i)
    for text, ident in buttons:
        w = 48 if len(text) <= 8 else 60
        rc.append(' PUSHBUTTON "%s", %s, %d, 4, %d, 14' % (text, ident, x, w))
        anchors.append((ident, 'kTopLeft'))
        x += w + 4
    rc.append(' EDITTEXT %d, 4, 24, 176, 12, ES_AUTOHSCROLL' % FILTER)
    anchors += [(str(FILTER), 'kTopLeft'), (str(LIST), 'kLeftTall')]
    columns_y = {1: 20, 2: 20}
    gid, fid, did, nid = GROUP0, FIELD0, DEPT0, NAME0
    body = []
    for column, caption, rows in page['groups']:
        gx = 188 if column == 1 else 396
        gy = columns_y[column]
        gh = 14 + 14 * len(rows) + 6
        body.append('')
        body.append(' GROUPBOX "%s", %d, %d, %d, 200, %d' % (caption, gid, gx, gy, gh))
        anchors.append((str(gid), 'kTopLeft'))
        gid += 1
        y = gy + 14
        for key, label, kind, chars, flags in rows:
            body.append(' LTEXT "%s", -1, %d, %d, 54, 8' % (label, gx + 8, y + 2))
            ex = gx + 64
            dept = name = 0
            if kind == 'account':
                body.append(' EDITTEXT %d, %d, %d, 44, 12, ES_UPPERCASE | ES_AUTOHSCROLL' % (fid, ex, y))
                body.append(' EDITTEXT %d, %d, %d, 20, 12, ES_UPPERCASE | ES_AUTOHSCROLL' % (did, ex + 46, y))
                body.append(' LTEXT "", %d, %d, %d, 60, 8, SS_ENDELLIPSIS | SS_NOPREFIX' % (nid, ex + 70, y + 2))
                dept, name = did, nid
                anchors += [(str(did), 'kTopLeft'), (str(nid), 'kTopLeft')]
                did += 1
                nid += 1
            elif kind == 'date':
                body.append(' CONTROL "", %d, "SysDateTimePick32", DTS_SHORTDATEFORMAT | DTS_SHOWNONE | WS_TABSTOP, '
                            '%d, %d, %d, 12' % (fid, ex, y, edit_width(kind, chars)))
            elif kind == 'choice':
                body.append(' COMBOBOX %d, %d, %d, %d, 80, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP'
                            % (fid, ex, y, edit_width(kind, chars)))
            else:
                style = {'upper': 'ES_UPPERCASE | ES_AUTOHSCROLL', 'number': 'ES_NUMBER | ES_AUTOHSCROLL',
                         'password': 'ES_PASSWORD | ES_AUTOHSCROLL', 'readonly': 'ES_READONLY | ES_AUTOHSCROLL',
                         'amount': 'ES_RIGHT | ES_AUTOHSCROLL', 'decimal': 'ES_RIGHT | ES_AUTOHSCROLL'}.get(
                    kind, 'ES_AUTOHSCROLL')
                body.append(' EDITTEXT %d, %d, %d, %d, 12, %s' % (fid, ex, y, edit_width(kind, chars), style))
            anchors.append((str(fid), 'kTopLeft'))
            flag = 'FieldFlag::Key' if 'k' in flags else 'FieldFlag::KeyPart' if 'K' in flags else \
                'FieldFlag::Required' if 'r' in flags else 'FieldFlag::None'
            fields.append('{%d, "%s", FieldKind::%s, %d, %s, %d, %d}' % (fid, key, KIND[kind], chars, flag, dept, name))
            fid += 1
            y += 14
        columns_y[column] = gy + gh + 4
    if page.get('commands'):
        top = 20
        body.append('')
        body.append(' GROUPBOX "The level\'s commands", %d, 396, %d, 200, %d' % (gid, top, MIN_HEIGHT - 24))
        body.append(' CONTROL "", %d, "SysListView32", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | '
                    'LVS_NOCOLUMNHEADER | WS_BORDER | WS_TABSTOP, 404, %d, 184, %d' % (COMMANDS, top + 14, MIN_HEIGHT - 44))
        anchors += [(str(gid), 'kAll'), (str(COMMANDS), 'kAll')]
        gid += 1
    height = max(MIN_HEIGHT, max(columns_y.values()) + 4)
    rc.insert(4 + len(extra), ' CONTROL "", %d, "SysListView32", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | '
                              'LVS_OWNERDATA | WS_BORDER | WS_TABSTOP, 4, 40, 176, %d' % (LIST, height - 44))
    text = ['%s DIALOGEX 0, 0, %d, %d' % (page['idd'], WIDTH, height), 'STYLE DS_CONTROL | WS_CHILD',
            'EXSTYLE WS_EX_CONTROLPARENT', 'FONT 9, "Segoe UI"', '{'] + rc + body + ['}', '']
    return text, fields, anchors, extra


def main(root):
    rc = ['//', '// pages.rc - generated by gl/res/tools/make_pages.py (spec #8 S8-M2). Do not edit:',
          '// change the PAGES table there and run it. #included by port.rc; plain ASCII.', '//', '']
    inc = ['// PageDescs.inc - generated by gl/res/tools/make_pages.py (spec #8 S8-M2). Do not edit.',
           '// The list-and-detail pages\' descriptors (RecordPage.h), included once by RecordPages.cpp.', '']
    for page in PAGES:
        text, fields, anchors, extra = build(page)
        rc += ['// ---- %s ' % page['title'] + '-' * (72 - len(page['title'])), ''] + text
        cols = ', '.join('{L"%s", %d, %s}' % (h, w, 'true' if r else 'false') for h, w, r in page['columns'])
        inc.append('const PageDesc k_%s{' % page['name'])
        inc.append('    %s, L"%s", %s,' % (page['idd'], page['title'], 'true' if page.get('commands') else 'false'))
        inc.append('    {%s},' % cols)
        inc.append('    {')
        inc += ['        %s,' % f for f in fields]
        inc.append('    },')
        inc.append('    {')
        line = '       '
        for ident, a in anchors:
            item = ' {%s, m::%s},' % (ident, a)
            if len(line) + len(item) > 110:
                inc.append(line)
                line = '       '
            line += item
        inc.append(line)
        inc.append('    },')
        inc.append('    {%s},' % ', '.join('{%d, L"%s"}' % (bid, t) for bid, (t, _) in zip(extra, page.get('buttons', []))))
        inc.append('};')
        inc.append('')
    with open(os.path.join(root, 'res', 'pages.rc'), 'w', newline='\r\n', encoding='ascii') as f:
        f.write('\n'.join(rc))
    with open(os.path.join(root, 'port', 'pages', 'PageDescs.inc'), 'w', newline='\r\n', encoding='ascii') as f:
        f.write('\n'.join(inc))
    print('pages: %d' % len(PAGES))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'gl')
