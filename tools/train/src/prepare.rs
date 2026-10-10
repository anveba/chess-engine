// Usage: prepare INPUT... OUTPUT

use std::{
    fs::{self, File},
    io::{BufReader, BufWriter, Read, Write},
};

use bullet_lib::{
    game::formats::bulletformat::{BulletFormat, ChessBoard},
    value::loader::viribinpack::{Filter, Game},
};
use rand::{seq::SliceRandom, Rng};

const BUCKETS: usize = 64;

fn splat(inputs: &[String], bucket_paths: &[String], rng: &mut impl Rng) -> (u64, u64) {
    let mut buckets: Vec<_> = bucket_paths
        .iter()
        .map(|path| BufWriter::new(File::create(path).unwrap()))
        .collect();
    let filter = Filter::default();
    let (mut games, mut positions) = (0, 0);
    for input in inputs {
        let mut reader = BufReader::new(File::open(input).unwrap());
        let mut moves = Vec::new();
        while let Ok(game) = Game::deserialise_from(&mut reader, moves) {
            game.splat_to_bulletformat(
                |board| {
                    positions += 1;
                    let bucket = &mut buckets[rng.random_range(0..BUCKETS)];
                    Ok(ChessBoard::write_to_bin(bucket, &[board])?)
                },
                &filter,
            )
            .unwrap();
            games += 1;
            moves = game.moves;
            moves.clear();
        }
    }
    (games, positions)
}

fn shuffle(bucket_paths: &[String], output: &str, rng: &mut impl Rng) {
    let mut out = BufWriter::new(File::create(output).unwrap());
    for path in bucket_paths {
        let len = fs::metadata(path).unwrap().len() as usize / size_of::<ChessBoard>();
        let mut boards = vec![[0u8; size_of::<ChessBoard>()]; len];
        File::open(path)
            .unwrap()
            .read_exact(boards.as_flattened_mut())
            .unwrap();
        boards.shuffle(rng);
        out.write_all(boards.as_flattened()).unwrap();
        fs::remove_file(path).unwrap();
    }
    out.flush().unwrap();
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        panic!("Usage: prepare INPUT... OUTPUT");
    }
    let output = &args[args.len() - 1];
    let bucket_paths: Vec<String> = (0..BUCKETS).map(|i| format!("{output}.tmp{i}")).collect();
    let mut rng = rand::rng();

    let (games, positions) = splat(&args[1..args.len() - 1], &bucket_paths, &mut rng);
    shuffle(&bucket_paths, output, &mut rng);
    println!("{games} games, {positions} positions");
}
