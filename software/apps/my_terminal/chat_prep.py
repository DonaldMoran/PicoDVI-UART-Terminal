import os

def split_file(input_file, output_file, chunk_size=4000):
    try:
        # Check if the input file exists in the current directory
        if not os.path.isfile(input_file):
            print(f"Error: The file '{input_file}' does not exist in the current directory.")
            return
        
        # Read the content of the input file
        with open(input_file, 'r') as infile:
            content = infile.read()

        # Calculate how many parts we will have
        num_parts = len(content) // chunk_size + (1 if len(content) % chunk_size > 0 else 0)

        # Write the chunks into the output file
        with open(output_file, 'w') as outfile:
            for part_num in range(num_parts):
                # Get the current chunk
                start = part_num * chunk_size
                end = start + chunk_size
                chunk = content[start:end]

                # Write part label and chunk to the output file
                outfile.write(f"Part {part_num + 1}\n")
                outfile.write(chunk)
                outfile.write("\n\n")  # Separate parts with extra newlines

        print(f"File split successfully into {num_parts} parts. The output is in '{output_file}'.")
    except Exception as e:
        print(f"An error occurred: {e}")

# Ask the user for the filename
input_file = input("Enter the input file name (must be in the same directory as this Python script): ")

# Check if the file exists in the same directory
if not os.path.isfile(input_file):
    print(f"Error: The file '{input_file}' was not found in the current directory.")
else:
    # Define the output file name
    output_file = 'output.txt'  # You can also ask the user for the output filename if desired

    # Call the split_file function
    split_file(input_file, output_file)
