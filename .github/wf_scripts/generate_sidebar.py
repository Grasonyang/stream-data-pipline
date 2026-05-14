#!/usr/bin/env python3
"""
Generate GitHub Wiki _Sidebar.md from _sidebar.yml

Usage: python3 generate_sidebar.py <input_yml> <output_md>
"""

import sys
import yaml


def generate_sidebar_md(sidebar_config, indent=0):
    """Recursively generate Markdown sidebar content"""
    md_lines = []
    
    for item in sidebar_config.get('items', []):
        title = item.get('title')
        page = item.get('page')
        sub_items = item.get('items', [])
        
        # Indentation for the current level
        prefix = '  ' * indent + '* '
        
        if page:
            # Item with a page link
            md_lines.append(f"{prefix}[{title}]({page})")
        else:
            # Category without a link
            md_lines.append(f"{prefix}**{title}**")
        
        # Recursively process sub-items
        if sub_items:
            md_lines.extend(generate_sidebar_md({'items': sub_items}, indent + 1))
    
    return md_lines


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 generate_sidebar.py <input_yml> <output_md>")
        sys.exit(1)
    
    yml_file = sys.argv[1]
    md_file = sys.argv[2]
    
    # Read the YAML file
    with open(yml_file, 'r', encoding='utf-8') as f:
        config = yaml.safe_load(f)
    
    # Generate Markdown content
    md_lines = []
    for section in config.get('sidebar', []):
        md_lines.extend(generate_sidebar_md(section))
    
    # Write to the Markdown file
    with open(md_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(md_lines))
    
    print(f"Generated {md_file} from {yml_file}")


if __name__ == '__main__':
    main()
