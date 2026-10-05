use bullet_lib::game::formats::bulletformat::ChessBoard;
use bullet_lib::game::inputs::SparseInputType;

const ENGINE_PIECE_INDEX: [usize; 6] = [
    0, // pawn
    2, // knight
    3, // bishop
    1, // rook
    4, // queen
    5, // king
];

#[derive(Clone, Copy, Debug, Default)]
pub struct Chess768EngineOrder;

impl SparseInputType for Chess768EngineOrder {
    type RequiredDataType = ChessBoard;

    fn num_inputs(&self) -> usize {
        768
    }

    fn max_active(&self) -> usize {
        32
    }

    fn map_features<F: FnMut(usize, usize)>(&self, pos: &Self::RequiredDataType, mut f: F) {
        for (piece, square) in pos.into_iter() {
            let colour = usize::from(piece & 8 != 0);
            let piece_offset = 64 * ENGINE_PIECE_INDEX[usize::from(piece & 7)];
            let square = usize::from(square);

            let stm = [0, 384][colour] + piece_offset + square;
            let ntm = [384, 0][colour] + piece_offset + (square ^ 56);
            f(stm, ntm)
        }
    }

    fn shorthand(&self) -> String {
        "768".to_string()
    }

    fn description(&self) -> String {
        "Chess768 inputs in the engine's piece order".to_string()
    }
}
