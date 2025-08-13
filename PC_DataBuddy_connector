import sys
import pandas as pd

def main():
    if len(sys.argv) < 2:
        print("Usage: python DataBuddy.py <csv_file>")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    try:
        data = pd.read_csv(csv_file)
        print(f"Loaded {csv_file}. Shape: {data.shape}")
        print("First 5 rows:")
        print(data.head())
        print("\nColumn stats:")
        print(data.describe(include='all'))
    except Exception as e:
        print(f"Error loading {csv_file}: {e}")

if __name__ == "__main__":
    main()
