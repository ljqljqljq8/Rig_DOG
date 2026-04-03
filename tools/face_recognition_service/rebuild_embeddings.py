from app import database


if __name__ == "__main__":
    database.rebuild()
    print(f"Built LBPH model with {len(database.label_to_name)} labels.")
