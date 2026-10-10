// Converts viriformat to bulletformat file, shuffles, and normalises the scale to the target.
// Usage: prepare --scale TARGET INPUT... OUTPUT

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
const FIT_SAMPLE: u64 = 2_000_000;

fn for_each_board(input: &str, mut f: impl FnMut(ChessBoard)) -> u64 {
    let filter = Filter::default();
    let mut reader = BufReader::new(File::open(input).unwrap());
    let mut moves = Vec::new();
    let mut games = 0;
    while let Ok(game) = Game::deserialise_from(&mut reader, moves) {
        game.splat_to_bulletformat(
            |board| {
                f(board);
                Ok(())
            },
            &filter,
        )
        .unwrap();
        games += 1;
        moves = game.moves;
        moves.clear();
    }
    games
}

fn fit_scale(input: &str, rng: &mut impl Rng) -> u32 {
    let max_positions = fs::metadata(input).unwrap().len() / 4; // each position takes at least 4 bytes
    let keep_prob = (FIT_SAMPLE as f64 / max_positions as f64).min(1.0);
    let mut sample = Vec::new();
    for_each_board(input, |board| {
        if rng.random_bool(keep_prob) {
            sample.push((f64::from(board.score), f64::from(board.result) / 2.0));
        }
    });

    let loss = |scale: u32| -> f64 {
        let squared_errors = sample.iter().map(|(score, result)| {
            let predicted = 1.0 / (1.0 + (-score / f64::from(scale)).exp());
            (result - predicted).powi(2)
        });
        squared_errors.sum::<f64>() / sample.len() as f64
    };
    let best = |scales: &mut dyn Iterator<Item = u32>| {
        scales.min_by(|a, b| loss(*a).total_cmp(&loss(*b))).unwrap()
    };
    let coarse = best(&mut (50..=2000).step_by(10));
    best(&mut (coarse - 10..=coarse + 10))
}

fn splat(inputs: &[(String, f32)], bucket_paths: &[String], rng: &mut impl Rng) -> (u64, u64) {
    let mut buckets: Vec<_> = bucket_paths
        .iter()
        .map(|path| BufWriter::new(File::create(path).unwrap()))
        .collect();
    let (mut games, mut positions) = (0, 0);
    for (input, factor) in inputs {
        games += for_each_board(input, |mut board| {
            positions += 1;
            board.score = (f32::from(board.score) * factor)
                .round()
                .clamp(-32000.0, 32000.0) as i16;
            let bucket = &mut buckets[rng.random_range(0..BUCKETS)];
            ChessBoard::write_to_bin(bucket, &[board]).unwrap();
        });
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
    if args.len() < 5 || args[1] != "--scale" {
        panic!("Usage: prepare --scale TARGET INPUT... OUTPUT");
    }
    let target: f32 = args[2].parse().unwrap();
    let rest = &args[3..];
    let output = &rest[rest.len() - 1];
    let bucket_paths: Vec<String> = (0..BUCKETS).map(|i| format!("{output}.tmp{i}")).collect();
    let mut rng = rand::rng();

    let inputs: Vec<(String, f32)> = rest[..rest.len() - 1]
        .iter()
        .map(|input| {
            let fitted = fit_scale(input, &mut rng);
            let factor = target / fitted as f32;
            println!("{input}: fitted scale {fitted}, scores multiplied by {factor:.3}");
            (input.clone(), factor)
        })
        .collect();
    let (games, positions) = splat(&inputs, &bucket_paths, &mut rng);
    shuffle(&bucket_paths, output, &mut rng);
    println!("{games} games, {positions} positions");
}
